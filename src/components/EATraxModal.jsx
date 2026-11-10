import React, { useState, useEffect, useRef } from 'react';

const TRACKLIST = [
  { id: 1, title: 'Riders on the Storm (Fredwreck Remix)', artist: 'Snoop Dogg feat. The Doors', genre: 'Hip-Hop', duration: '6:22', bpm: 105 },
  { id: 2, title: 'I Do', artist: 'Chingy', genre: 'Hip-Hop', duration: '3:58', bpm: 98 },
  { id: 3, title: 'Lean Back', artist: 'Terror Squad', genre: 'Hip-Hop', duration: '4:07', bpm: 95 },
  { id: 4, title: 'LAX', artist: 'Xzibit', genre: 'Hip-Hop', duration: '3:44', bpm: 96 },
  { id: 5, title: 'That\'z My Name', artist: 'Sly Boogy', genre: 'Hip-Hop', duration: '3:58', bpm: 100 },
  { id: 6, title: 'Switch/Twitch', artist: 'Fluke', genre: 'Electronic', duration: '9:33', bpm: 130 },
  { id: 7, title: 'Rush Hour', artist: 'Christopher Lawrence', genre: 'Electronic', duration: '7:47', bpm: 138 },
  { id: 8, title: 'Rocket Ride (Soulwax Remix)', artist: 'Felix Da Housecat', genre: 'Electronic', duration: '6:38', bpm: 128 },
  { id: 9, title: 'Hard EBM', artist: 'Sin', genre: 'Electronic', duration: '4:21', bpm: 132 },
  { id: 10, title: 'Mind Killer (Jagz Kooner Remix)', artist: 'FREELAND', genre: 'Electronic', duration: '6:42', bpm: 126 },
  { id: 11, title: 'Nothing But You (Cirrus Remix)', artist: 'Paul Van Dyk', genre: 'Electronic', duration: '7:01', bpm: 136 },
  { id: 12, title: 'Back on a Mission', artist: 'Cirrus', genre: 'Breakbeat', duration: '3:38', bpm: 132 },
  { id: 13, title: 'The Death & Resurrection Show', artist: 'Killing Joke', genre: 'Rock', duration: '6:56', bpm: 124 },
  { id: 14, title: 'Give It All', artist: 'Rise Against', genre: 'Rock', duration: '2:50', bpm: 195 },
  { id: 15, title: 'Scavenger', artist: 'Killradio', genre: 'Rock', duration: '2:32', bpm: 160 },
  { id: 16, title: 'Notice of Eviction', artist: 'The Bronx', genre: 'Rock', duration: '2:15', bpm: 180 },
  { id: 17, title: 'In My Head', artist: 'Queens of the Stone Age', genre: 'Rock', duration: '4:01', bpm: 118 },
  { id: 18, title: 'Determined', artist: 'Mudvayne', genre: 'Metal', duration: '2:39', bpm: 172 },
  { id: 19, title: 'Crashing Foreign Cars', artist: 'Helmet', genre: 'Rock', duration: '2:31', bpm: 145 },
  { id: 20, title: 'Black Betty', artist: 'Spiderbait', genre: 'Rock', duration: '3:26', bpm: 115 },
  { id: 21, title: 'I Need Speed', artist: 'Capone', genre: 'Hip-Hop', duration: '3:31', bpm: 95 },
  { id: 22, title: 'The Resonator', artist: 'UNKLE', genre: 'Electronic', duration: '6:08', bpm: 122 },
  { id: 23, title: 'No W', artist: 'Ministry', genre: 'Metal', duration: '3:14', bpm: 150 },
  { id: 24, title: 'I Am Weightless', artist: 'Septembre', genre: 'Rock', duration: '3:30', bpm: 120 }
];

export default function EATraxModal({ isOpen, onClose }) {
  const [currentTrackIndex, setCurrentTrackIndex] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const [genreFilter, setGenreFilter] = useState('All');
  const [volume, setVolume] = useState(0.7);
  const [audioPos, setAudioPos] = useState(14);

  const audioCtxRef = useRef(null);
  const oscRef = useRef(null);
  const gainRef = useRef(null);

  const filteredTracks = genreFilter === 'All'
    ? TRACKLIST
    : TRACKLIST.filter((t) => t.genre.toLowerCase().includes(genreFilter.toLowerCase()));

  const currentTrack = TRACKLIST[currentTrackIndex] || TRACKLIST[0];

  useEffect(() => {
    let interval;
    if (isPlaying) {
      interval = setInterval(() => {
        setAudioPos((prev) => (prev >= 240 ? 0 : prev + 1));
      }, 1000);
    }
    return () => clearInterval(interval);
  }, [isPlaying]);

  useEffect(() => {
    if (!isOpen && isPlaying) {
      stopSynthesizer();
    }
  }, [isOpen]);

  const startSynthesizer = () => {
    try {
      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      if (!AudioCtx) return;
      if (!audioCtxRef.current) {
        audioCtxRef.current = new AudioCtx();
      }
      const ctx = audioCtxRef.current;
      if (ctx.state === 'suspended') {
        ctx.resume();
      }

      if (oscRef.current) {
        try { oscRef.current.stop(); } catch (e) {}
      }

      const osc = ctx.createOscillator();
      const gain = ctx.createGain();

      const baseFreq = 55 + (currentTrack.bpm % 40);
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(baseFreq, ctx.currentTime);

      gain.gain.setValueAtTime(0.04 * volume, ctx.currentTime);

      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start();

      oscRef.current = osc;
      gainRef.current = gain;
      setIsPlaying(true);
    } catch (e) {
      setIsPlaying(true);
    }
  };

  const stopSynthesizer = () => {
    if (oscRef.current) {
      try {
        oscRef.current.stop();
        oscRef.current.disconnect();
      } catch (e) {}
      oscRef.current = null;
    }
    setIsPlaying(false);
  };

  const togglePlay = () => {
    if (isPlaying) {
      stopSynthesizer();
    } else {
      startSynthesizer();
    }
  };

  const handleNext = () => {
    const nextIdx = (currentTrackIndex + 1) % TRACKLIST.length;
    setCurrentTrackIndex(nextIdx);
    setAudioPos(0);
    if (isPlaying) {
      stopSynthesizer();
      setTimeout(startSynthesizer, 100);
    }
  };

  const handlePrev = () => {
    const prevIdx = (currentTrackIndex - 1 + TRACKLIST.length) % TRACKLIST.length;
    setCurrentTrackIndex(prevIdx);
    setAudioPos(0);
    if (isPlaying) {
      stopSynthesizer();
      setTimeout(startSynthesizer, 100);
    }
  };

  const selectTrack = (id) => {
    const idx = TRACKLIST.findIndex((t) => t.id === id);
    if (idx !== -1) {
      setCurrentTrackIndex(idx);
      setAudioPos(0);
      if (isPlaying) {
        stopSynthesizer();
        setTimeout(startSynthesizer, 100);
      }
    }
  };

  if (!isOpen) return null;

  const formatSeconds = (sec) => {
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    return `${m}:${s < 10 ? '0' : ''}${s}`;
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-window"
        style={{
          width: 580,
          background: 'linear-gradient(180deg, #1b251b 0%, #0d140d 60%, #050805 100%)',
          border: '2px solid #a1d464',
          borderRadius: 4,
          padding: 16,
          boxShadow: '0 0 25px rgba(0, 255, 100, 0.45)',
          color: '#ffffff'
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #334433', paddingBottom: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ color: '#00ff66', fontSize: 13 }}>&#9654;</span>
            <span style={{ fontFamily: 'Impact, Arial Black', fontStyle: 'italic', fontSize: 16, letterSpacing: 1.5, color: '#ffffff' }}>
              EA TRAX &bull; NFSU2 SOUNDTRACK
            </span>
          </div>
          <button
            onClick={onClose}
            style={{
              background: '#222222',
              border: '1px solid #555555',
              color: '#cccccc',
              cursor: 'pointer',
              fontWeight: 'bold',
              padding: '2px 8px'
            }}
          >
            [X]
          </button>
        </div>

        <div style={{ marginTop: 12, padding: 12, background: '#0a100a', border: '1px solid #233523', borderRadius: 3 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
            <div>
              <div style={{ fontSize: 9, color: '#a1d464', fontWeight: 'bold', textTransform: 'uppercase', letterSpacing: 1 }}>
                NOW PLAYING [{currentTrack.genre}] &bull; TRACK #{currentTrack.id}
              </div>
              <div style={{ fontFamily: 'Impact, Arial Black', fontSize: 18, color: '#ffffff', fontStyle: 'italic', marginTop: 2 }}>
                {currentTrack.title}
              </div>
              <div style={{ fontSize: 11, color: '#cce8a8', marginTop: 2 }}>
                {currentTrack.artist}
              </div>
            </div>

            <div style={{ display: 'flex', gap: 2, alignItems: 'flex-end', height: 28, padding: '0 4px' }}>
              {[18, 24, 12, 28, 16, 22, 10, 26, 14, 20, 27, 15].map((h, i) => (
                <div
                  key={i}
                  style={{
                    width: 3,
                    height: isPlaying ? `${Math.max(4, (h * ((i + audioPos) % 5 + 1)) / 4)}px` : '4px',
                    backgroundColor: i % 3 === 0 ? '#00ff66' : '#a1d464',
                    transition: 'height 0.15s ease'
                  }}
                />
              ))}
            </div>
          </div>

          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 12, borderTop: '1px solid #1a2a1a', paddingTop: 8 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <button
                onClick={handlePrev}
                style={{ background: '#1e2d1e', border: '1px solid #3d5c3d', color: '#ffffff', cursor: 'pointer', padding: '3px 8px', fontSize: 11 }}
              >
                |&#9664;
              </button>
              <button
                onClick={togglePlay}
                style={{
                  background: isPlaying ? '#a1d464' : '#1e2d1e',
                  color: isPlaying ? '#000000' : '#ffffff',
                  border: '1px solid #5a8a5a',
                  cursor: 'pointer',
                  fontWeight: 'bold',
                  padding: '3px 12px',
                  fontSize: 11
                }}
              >
                {isPlaying ? 'PAUSE' : '&#9654; PLAY'}
              </button>
              <button
                onClick={handleNext}
                style={{ background: '#1e2d1e', border: '1px solid #3d5c3d', color: '#ffffff', cursor: 'pointer', padding: '3px 8px', fontSize: 11 }}
              >
                &#9654;|
              </button>
              <span style={{ fontSize: 9, color: '#888888', marginLeft: 6 }}>
                {formatSeconds(audioPos)} / {currentTrack.duration}
              </span>
            </div>

            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <span style={{ fontSize: 9, color: '#888888' }}>VOL</span>
              <input
                type="range"
                min="0"
                max="1"
                step="0.05"
                value={volume}
                onChange={(e) => {
                  const v = parseFloat(e.target.value);
                  setVolume(v);
                  if (gainRef.current && audioCtxRef.current) {
                    gainRef.current.gain.setValueAtTime(0.04 * v, audioCtxRef.current.currentTime);
                  }
                }}
                style={{ width: 60, accentColor: '#a1d464' }}
              />
              <a
                href={`https://www.youtube.com/results?search_query=${encodeURIComponent(currentTrack.artist + ' ' + currentTrack.title)}`}
                target="_blank"
                rel="noreferrer"
                style={{
                  color: '#a1d464',
                  fontSize: 9,
                  textDecoration: 'none',
                  border: '1px solid #3d5c3d',
                  padding: '2px 6px',
                  borderRadius: 2
                }}
                title="Search on YouTube"
              >
                Open Full Song &#8599;
              </a>
            </div>
          </div>
        </div>

        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 10, marginBottom: 6 }}>
          <span style={{ fontSize: 9, color: '#aaaaaa', fontWeight: 'bold' }}>
            ORIGINAL 2004 SOUNDTRACK LIST (24 TRACKS)
          </span>
          <div style={{ display: 'flex', gap: 4 }}>
            {['All', 'Hip-Hop', 'Rock', 'Electronic'].map((genre) => (
              <button
                key={genre}
                onClick={() => setGenreFilter(genre)}
                style={{
                  background: genreFilter === genre ? '#a1d464' : '#141e14',
                  color: genreFilter === genre ? '#000000' : '#888888',
                  border: '1px solid #2d442d',
                  fontSize: 8,
                  padding: '2px 6px',
                  cursor: 'pointer'
                }}
              >
                {genre}
              </button>
            ))}
          </div>
        </div>

        <div style={{ maxHeight: 200, overflowY: 'auto', background: '#0a100a', border: '1px solid #233523', padding: 4 }}>
          {filteredTracks.map((t) => {
            const isSelected = t.id === currentTrack.id;
            return (
              <div
                key={t.id}
                onClick={() => selectTrack(t.id)}
                style={{
                  display: 'flex',
                  justifyContent: 'space-between',
                  alignItems: 'center',
                  padding: '4px 8px',
                  backgroundColor: isSelected ? 'rgba(161, 212, 100, 0.2)' : 'transparent',
                  borderLeft: isSelected ? '3px solid #00ff66' : '3px solid transparent',
                  cursor: 'pointer',
                  borderBottom: '1px solid #141f14'
                }}
                onMouseEnter={(e) => {
                  if (!isSelected) e.currentTarget.style.backgroundColor = 'rgba(255, 255, 255, 0.05)';
                }}
                onMouseLeave={(e) => {
                  if (!isSelected) e.currentTarget.style.backgroundColor = 'transparent';
                }}
              >
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ color: isSelected ? '#00ff66' : '#666666', fontSize: 9, width: 14 }}>
                    {isSelected ? '▶' : t.id}
                  </span>
                  <div>
                    <span style={{ color: isSelected ? '#ffffff' : '#dddddd', fontSize: 10, fontWeight: isSelected ? 'bold' : 'normal' }}>
                      {t.title}
                    </span>
                    <span style={{ color: '#888888', fontSize: 9, marginLeft: 6 }}>
                      — {t.artist}
                    </span>
                  </div>
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <span style={{ fontSize: 8, color: '#779977', background: '#121e12', padding: '1px 4px', borderRadius: 2 }}>
                    {t.genre}
                  </span>
                  <span style={{ fontSize: 9, color: '#777777', width: 28, textAlign: 'right' }}>
                    {t.duration}
                  </span>
                </div>
              </div>
            );
          })}
        </div>

        <div style={{ textAlign: 'center', marginTop: 10, fontSize: 8, color: '#667766' }}>
          EA TRAX is a trademark of Electronic Arts Inc. All musical compositions property of their respective copyright holders.
        </div>
      </div>
    </div>
  );
}
