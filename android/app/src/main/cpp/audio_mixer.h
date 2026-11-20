#pragma once
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <dlfcn.h>
#include "audio_resampler.h"
#include "graphics_cache.h"
struct PcmVoice {
    std::vector<uint8_t> samples;
    unsigned channels=2,bits=16,rate=48000;
    std::atomic<uint32_t> frequency{48000};
    std::atomic<double> position{0};
    std::atomic<float> left{1.0f}, right{1.0f};
    std::atomic<bool> playing{false}, loop{false};
    // Streaming: a looping buffer the guest rewrites while it plays (music,
    // speech, the engine mix). Frames played since the last rewrite; once that
    // passes the whole ring the guest is not keeping up (loading screens hold
    // the machine) and the ring would repeat its last half second until it
    // resumes, so it is muted instead. Loops written once (sound effects) never
    // become streaming and keep looping.
    unsigned publishes=0;
    double staleFrames=0;

    PcmVoice() = default;
    PcmVoice(const PcmVoice& o)
        : samples(o.samples), channels(o.channels), bits(o.bits), rate(o.rate),
          frequency(o.frequency.load(std::memory_order_relaxed)),
          position(o.position.load(std::memory_order_relaxed)),
          left(o.left.load(std::memory_order_relaxed)),
          right(o.right.load(std::memory_order_relaxed)),
          playing(o.playing.load(std::memory_order_relaxed)),
          loop(o.loop.load(std::memory_order_relaxed)),
          publishes(o.publishes), staleFrames(o.staleFrames) {}

    PcmVoice& operator=(const PcmVoice& o) {
        if (this != &o) {
            samples = o.samples;
            channels = o.channels;
            bits = o.bits;
            rate = o.rate;
            frequency.store(o.frequency.load(std::memory_order_relaxed), std::memory_order_relaxed);
            position.store(o.position.load(std::memory_order_relaxed), std::memory_order_relaxed);
            left.store(o.left.load(std::memory_order_relaxed), std::memory_order_relaxed);
            right.store(o.right.load(std::memory_order_relaxed), std::memory_order_relaxed);
            playing.store(o.playing.load(std::memory_order_relaxed), std::memory_order_relaxed);
            loop.store(o.loop.load(std::memory_order_relaxed), std::memory_order_relaxed);
            publishes = o.publishes;
            staleFrames = o.staleFrames;
        }
        return *this;
    }

    bool streaming()const{return loop.load(std::memory_order_relaxed)&&publishes>=4;}
    void published(){staleFrames=0;if(playing.load(std::memory_order_relaxed))++publishes;}
    unsigned block()const{return channels*(bits/8);}
};
class PcmMixer {
public:
    PcmMixer(){(void)nfs_audio::upsampleKernel();} // No filter construction in the real-time callback.
    std::mutex mutex;
    std::array<std::shared_ptr<PcmVoice>,128> voices{};
    unsigned rate=48000;
    // Protected by mutex, including callback updates. Inspect before clamping.
    uint64_t clippedSamples=0;float peakAmplitude=0;
    bool add(const std::shared_ptr<PcmVoice>& voice){
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& slot:voices)if(!slot){slot=voice;return true;}return false;
    }
    void remove(const std::shared_ptr<PcmVoice>& voice){std::lock_guard<std::mutex> lock(mutex);for(auto& slot:voices)if(slot==voice)slot.reset();}
    static float sample(const PcmVoice& voice,size_t frame,unsigned channel){
        size_t offset=frame*voice.block()+std::min(channel,voice.channels-1)*(voice.bits/8);
        if(voice.bits==8)return (int(voice.samples[offset])-128)/128.f;
        uint16_t word=uint16_t(voice.samples[offset])|(uint16_t(voice.samples[offset+1])<<8);
        return static_cast<int16_t>(word)/32768.f;
    }
    void render(float* output,unsigned frames){
        std::fill(output,output+frames*2,0.f);
        const auto& kernel=nfs_audio::upsampleKernel();
        // The real-time audio callback must never suffer priority inversion
        // or block indefinitely on the guest emulation thread.
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        bool acquired = false;
        for(int spin=0; spin<10; ++spin){
            if(lock.try_lock()){ acquired = true; break; }
            #if defined(__aarch64__)
            asm volatile("yield");
            #endif
        }
        if(!acquired){
            // Guest is actively uploading a buffer slice; avoid underrun stall.
            return;
        }
        for(auto& voice:voices){
            if(!voice||!voice->playing.load(std::memory_order_relaxed)||voice->samples.empty())continue;
            size_t count=voice->samples.size()/voice->block();if(!count)continue;
            double step=double(voice->frequency.load(std::memory_order_relaxed))/rate;
            double cur_pos=voice->position.load(std::memory_order_relaxed);
            if(voice->streaming()&&voice->staleFrames>=double(count)){
                voice->position.store(std::fmod(cur_pos+step*frames,double(count)), std::memory_order_relaxed);
                continue;
            }
            voice->staleFrames+=step*frames;
            static auto s_render_voice = reinterpret_cast<int(*)(
                float*, uint32_t, const VoiceDesc*, const float*, uint32_t, uint32_t, double*
            )>(dlsym(RTLD_DEFAULT, "nfs_audio_mixer_render_voice"));
            if(s_render_voice){
                VoiceDesc desc;
                desc.samples = voice->samples.data();
                desc.bits = voice->bits;
                desc.channels = voice->channels;
                desc.total_frames = count;
                desc.position = cur_pos;
                desc.step = step;
                desc.loop_mode = voice->loop.load(std::memory_order_relaxed) ? 1 : 0;
                desc.gain_l = voice->left.load(std::memory_order_relaxed);
                desc.gain_r = voice->right.load(std::memory_order_relaxed);

                double next_pos = cur_pos;
                int still = s_render_voice(
                    output, frames, &desc,
                    kernel.weights[0].data(),
                    nfs_audio::UpsampleKernel::phases,
                    nfs_audio::UpsampleKernel::taps,
                    &next_pos
                );
                voice->position.store(next_pos, std::memory_order_relaxed);
                if(!still) { voice->playing.store(false, std::memory_order_relaxed); }
                continue;
            }
            for(unsigned frame=0;frame<frames;++frame){
                cur_pos=voice->position.load(std::memory_order_relaxed);
                if(cur_pos>=count){
                    if(voice->loop.load(std::memory_order_relaxed)) {
                        cur_pos=std::fmod(cur_pos,double(count));
                        voice->position.store(cur_pos, std::memory_order_relaxed);
                    } else {
                        voice->playing.store(false, std::memory_order_relaxed);
                        voice->position.store(0, std::memory_order_relaxed);
                        break;
                    }
                }
                size_t a=size_t(cur_pos),b=a+1;float fraction=float(cur_pos-a);
                if(b==count)b=voice->loop.load(std::memory_order_relaxed)?0:a;
                float v_left = voice->left.load(std::memory_order_relaxed);
                float v_right = voice->right.load(std::memory_order_relaxed);
                if(step<1){
                    float phase=fraction*nfs_audio::UpsampleKernel::phases;
                    unsigned index=std::min(unsigned(phase),nfs_audio::UpsampleKernel::phases-1);
                    float blend=phase-index,left=0,right=0;
                    float samples_l[16], samples_r[16];
                    for(unsigned tap=0;tap<nfs_audio::UpsampleKernel::taps;++tap){
                        int64_t source=int64_t(a)+int(tap)-7;
                        if(source<0||uint64_t(source)>=count){
                            if(voice->loop.load(std::memory_order_relaxed)){source%=int64_t(count);if(source<0)source+=count;}
                            else source=std::clamp(source,int64_t(0),int64_t(count)-1);
                        }
                        samples_l[tap]=sample(*voice,size_t(source),0);
                        samples_r[tap]=sample(*voice,size_t(source),1);
                    }
                    static auto s_simd_resample = reinterpret_cast<void(*)(const float*,const float*,const float*,const float*,float,float*,float*)>(dlsym(RTLD_DEFAULT,"nfs_audio_resample_sinc_simd"));
                    if(s_simd_resample){
                        s_simd_resample(samples_l, samples_r, kernel.weights[index].data(), kernel.weights[index+1].data(), blend, &left, &right);
                    }else{
                        for(unsigned tap=0;tap<nfs_audio::UpsampleKernel::taps;++tap){
                            float weight=kernel.weights[index][tap]+(kernel.weights[index+1][tap]-kernel.weights[index][tap])*blend;
                            left+=samples_l[tap]*weight;right+=samples_r[tap]*weight;
                        }
                    }
                    output[frame*2]+=left*v_left;output[frame*2+1]+=right*v_right;
                }else {
                    static auto s_render_linear = reinterpret_cast<int(*)(
                        float*, uint32_t, const uint8_t*, uint32_t, uint32_t, size_t, double, double, int, float, float, double*
                    )>(dlsym(RTLD_DEFAULT, "nfs_audio_mixer_render_linear"));
                    if (s_render_linear) {
                        double next_pos = cur_pos;
                        int still = s_render_linear(
                            output + frame * 2, frames - frame,
                            voice->samples.data(), voice->bits, voice->channels, count,
                            cur_pos, step, voice->loop.load(std::memory_order_relaxed) ? 1 : 0,
                            v_left, v_right, &next_pos
                        );
                        voice->position.store(next_pos, std::memory_order_relaxed);
                        if (!still) { voice->playing.store(false, std::memory_order_relaxed); }
                        break;
                    }
                    for(unsigned channel=0;channel<2;++channel){float first=sample(*voice,a,channel),second=sample(*voice,b,channel);
                        output[frame*2+channel]+=(first+(second-first)*fraction)*(channel?v_right:v_left);}
                    voice->position.store(cur_pos+step, std::memory_order_relaxed);
                }
            }
        }
        static auto s_clamp_stats = reinterpret_cast<void(*)(float*, size_t, float*, uint64_t*)>(
            dlsym(RTLD_DEFAULT, "nfs_audio_mixer_clamp_and_stats")
        );
        if (s_clamp_stats) {
            s_clamp_stats(output, frames * 2, &peakAmplitude, &clippedSamples);
        } else {
            for(unsigned i=0;i<frames*2;++i){
                float amplitude=std::abs(output[i]);peakAmplitude=std::max(peakAmplitude,amplitude);
                if(amplitude>1.f)++clippedSamples;
                output[i]=std::clamp(output[i],-1.f,1.f);
            }
        }
    }
    void advance(unsigned frames){
        if(!frames) return;
        std::unique_lock<std::mutex> lock(mutex, std::defer_lock);
        if(!lock.try_lock()) return;
        static auto s_advance_voices = reinterpret_cast<void(*)(VoiceAdvanceState*, size_t, uint32_t)>(
            dlsym(RTLD_DEFAULT, "nfs_audio_mixer_advance_voices")
        );
        if(s_advance_voices){
            std::array<VoiceAdvanceState, 128> batch{};
            std::array<PcmVoice*, 128> active_ptrs{};
            size_t batch_count = 0;
            for(auto& voice:voices){
                if(!voice||!voice->playing.load(std::memory_order_relaxed)||voice->samples.empty())continue;
                size_t count=voice->samples.size()/voice->block();if(!count)continue;
                auto& st = batch[batch_count];
                st.position = voice->position.load(std::memory_order_relaxed);
                st.stale_frames = voice->staleFrames;
                st.step = double(voice->frequency.load(std::memory_order_relaxed))/rate;
                st.count = count;
                st.loop_mode = voice->loop.load(std::memory_order_relaxed) ? 1 : 0;
                st.is_streaming = voice->streaming() ? 1 : 0;
                st.is_playing = 1;
                active_ptrs[batch_count] = voice.get();
                batch_count++;
            }
            if(batch_count > 0){
                s_advance_voices(batch.data(), batch_count, frames);
                for(size_t i=0; i<batch_count; ++i){
                    auto* v = active_ptrs[i];
                    v->position.store(batch[i].position, std::memory_order_relaxed);
                    v->staleFrames = batch[i].stale_frames;
                    if(!batch[i].is_playing){
                        v->playing.store(false, std::memory_order_relaxed);
                    }
                }
                return;
            }
        }
        for(auto& voice:voices){
            if(!voice||!voice->playing.load(std::memory_order_relaxed)||voice->samples.empty())continue;
            size_t count=voice->samples.size()/voice->block();if(!count)continue;
            double step=double(voice->frequency.load(std::memory_order_relaxed))/rate;
            double cur_pos=voice->position.load(std::memory_order_relaxed);
            if(voice->streaming()&&voice->staleFrames>=double(count)){
                voice->position.store(std::fmod(cur_pos+step*frames,double(count)), std::memory_order_relaxed);
                continue;
            }
            voice->staleFrames+=step*frames;
            cur_pos+=step*frames;
            if(cur_pos>=count){
                if(voice->loop.load(std::memory_order_relaxed)) voice->position.store(std::fmod(cur_pos,double(count)), std::memory_order_relaxed);
                else{voice->playing.store(false, std::memory_order_relaxed);voice->position.store(0, std::memory_order_relaxed);}
            } else {
                voice->position.store(cur_pos, std::memory_order_relaxed);
            }
        }
    }
};
