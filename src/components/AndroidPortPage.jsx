import React from 'react';

export default function AndroidPortPage({ onNavigate }) {
  const repoUrl = 'https://github.com/mininxd/NFSU2-Revived-Android';
  const mainBranchUrl = 'https://github.com/mininxd/NFSU2-Revived-Android/tree/main';
  const releasesUrl = 'https://github.com/mininxd/NFSU2-Revived-Android/releases';

  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 620, fontFamily: 'verdana, arial, sans-serif' }}>
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="/assets/images.ea.com/eagames/official/nfs/underground2/us/universal/hdr_features.jpg"
          width="615"
          alt="Android Port"
          style={{ display: 'block' }}
        />
      </div>

      <div style={{ padding: '12px 18px', background: '#1c261c', borderBottom: '2px solid #a1d464', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div>
          <span style={{ color: '#00ff66', fontSize: 10, fontWeight: 'bold' }}>&#9679; OPEN SOURCE REVIVAL</span>
          <div style={{ fontFamily: 'Impact, Arial Black', fontStyle: 'italic', fontSize: 18, color: '#ffffff', letterSpacing: 1 }}>
            NFSU2 REVIVED &bull; ANDROID EDITION
          </div>
        </div>
        <div style={{ textAlign: 'right' }}>
          <span style={{ background: '#00ff66', color: '#000000', fontSize: 9, fontWeight: 'bold', padding: '2px 6px', borderRadius: 2 }}>
            MAIN BRANCH
          </span>
        </div>
      </div>

      <div style={{ padding: '16px 20px' }}>
        <div
          style={{
            background: 'linear-gradient(135deg, #1b261b 0%, #0d160d 70%, #050a05 100%)',
            border: '2px solid #3d593d',
            borderRadius: 3,
            padding: 16,
            color: '#ffffff',
            boxShadow: '0 4px 15px rgba(0,0,0,0.5)'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
            <div>
              <div style={{ fontSize: 9, color: '#a1d464', fontWeight: 'bold', textTransform: 'uppercase', letterSpacing: 1 }}>
                REVERSE-ENGINEERED NATIVE ANDROID PORT
              </div>
              <div style={{ fontFamily: 'Impact, Arial Black', fontSize: 22, color: '#ffffff', fontStyle: 'italic', marginTop: 2 }}>
                EXPERIENCE BAYVIEW ON MOBILE
              </div>
            </div>
            <a
              href={releasesUrl}
              target="_blank"
              rel="noreferrer"
              style={{
                background: 'linear-gradient(90deg, #a1d464 0%, #00ff66 100%)',
                color: '#000000',
                textDecoration: 'none',
                fontFamily: 'Impact, Arial Black',
                fontStyle: 'italic',
                fontSize: 13,
                letterSpacing: 1,
                padding: '6px 14px',
                borderRadius: 2,
                boxShadow: '0 0 10px rgba(0, 255, 100, 0.5)',
                cursor: 'pointer'
              }}
            >
              DOWNLOAD APK &#8599;
            </a>
          </div>

          <p style={{ fontSize: 10, lineHeight: 1.6, color: '#d0dfd0', marginTop: 12 }}>
            This repository contains the community reverse-engineered native Android port of <i>Need for Speed Underground 2</i>.
            Built to run authentically on modern ARM64 Android devices with high-framerate rendering, customizable touch controls, and full physical controller support.
          </p>

          <div style={{ display: 'flex', gap: 10, marginTop: 12 }}>
            <a
              href={mainBranchUrl}
              target="_blank"
              rel="noreferrer"
              style={{
                background: '#121e12',
                border: '1px solid #a1d464',
                color: '#ffffff',
                textDecoration: 'none',
                fontSize: 9.5,
                fontWeight: 'bold',
                padding: '5px 12px',
                borderRadius: 2,
                display: 'inline-flex',
                alignItems: 'center',
                gap: 6
              }}
            >
              <span>GitHub Repo: <b>main branch</b></span> &#8599;
            </a>
            <a
              href={releasesUrl}
              target="_blank"
              rel="noreferrer"
              style={{
                background: '#121e12',
                border: '1px solid #5a8a5a',
                color: '#cce8a8',
                textDecoration: 'none',
                fontSize: 9.5,
                padding: '5px 12px',
                borderRadius: 2
              }}
            >
              View APK Releases &amp; Changelogs &#8599;
            </a>
          </div>
        </div>

        <div style={{ marginTop: 18 }}>
          <b style={{ fontSize: 12, color: '#222222' }}>PORT FEATURES &amp; ENHANCEMENTS:</b>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, marginTop: 10 }}>
            <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
              <b style={{ fontSize: 10, color: '#1a331a' }}>NATIVE ARM64 RENDERING</b>
              <p style={{ fontSize: 9, lineHeight: 1.5, color: '#444444', margin: '4px 0 0 0' }}>
                Optimized Vulkan and OpenGL ES 3.0 graphic backends capable of locked 60 FPS gameplay on modern mobile chipsets.
              </p>
            </div>
            <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
              <b style={{ fontSize: 10, color: '#1a331a' }}>CUSTOM TOUCH CONTROLS</b>
              <p style={{ fontSize: 9, lineHeight: 1.5, color: '#444444', margin: '4px 0 0 0' }}>
                Full on-screen HUD with responsive steering thumbstick/arrows, analog throttle, nitrous button, and handbrake.
              </p>
            </div>
            <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
              <b style={{ fontSize: 10, color: '#1a331a' }}>BLUETOOTH GAMEPAD SUPPORT</b>
              <p style={{ fontSize: 9, lineHeight: 1.5, color: '#444444', margin: '4px 0 0 0' }}>
                Plug-and-play support for Xbox Wireless, PlayStation DualShock 4 / DualSense, Razer Kishi, and 8BitDo controllers.
              </p>
            </div>
            <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
              <b style={{ fontSize: 10, color: '#1a331a' }}>WIDESCREEN 16:9 / 20:9</b>
              <p style={{ fontSize: 9, lineHeight: 1.5, color: '#444444', margin: '4px 0 0 0' }}>
                Native widescreen resolution support that fits modern notched and edge-to-edge Android displays without distortion.
              </p>
            </div>
          </div>
        </div>

        <div style={{ marginTop: 18 }}>
          <b style={{ fontSize: 12, color: '#222222' }}>HARDWARE SPECIFICATIONS:</b>
          <table cellPadding="6" cellSpacing="0" border="1" borderColor="#cccccc" width="100%" style={{ borderCollapse: 'collapse', marginTop: 8, fontSize: 9.5 }}>
            <thead style={{ background: '#334433', color: '#ffffff' }}>
              <tr>
                <th align="left">Component</th>
                <th align="left">Minimum (30 FPS 720p)</th>
                <th align="left">Recommended (60 FPS 1080p)</th>
              </tr>
            </thead>
            <tbody>
              <tr bgcolor="#f9f9f9">
                <td><b>Operating System</b></td>
                <td>Android 8.0 (Oreo)</td>
                <td>Android 11.0 or newer</td>
              </tr>
              <tr bgcolor="#ffffff">
                <td><b>Processor</b></td>
                <td>Quad-Core 2.0 GHz (Snapdragon 660 / Helio G80)</td>
                <td>Snapdragon 845 / 865 / 8 Gen 1 / Dimensity 1200+</td>
              </tr>
              <tr bgcolor="#f9f9f9">
                <td><b>RAM</b></td>
                <td>3 GB</td>
                <td>4 GB to 8 GB</td>
              </tr>
              <tr bgcolor="#ffffff">
                <td><b>GPU</b></td>
                <td>Adreno 506 / Mali-G52</td>
                <td>Adreno 630 / Mali-G77 or higher</td>
              </tr>
              <tr bgcolor="#f9f9f9">
                <td><b>Storage</b></td>
                <td>2.0 GB free internal space</td>
                <td>Fast UFS 2.1 / 3.1 internal storage</td>
              </tr>
            </tbody>
          </table>
        </div>

        <div style={{ marginTop: 18, background: '#f0f5ee', border: '1px solid #88aa88', padding: 12, borderRadius: 2 }}>
          <b style={{ fontSize: 11, color: '#1f381f' }}>INSTALLATION &amp; SETUP GUIDE:</b>
          <ol style={{ fontSize: 9.5, lineHeight: 1.6, margin: '6px 0 0 0', paddingLeft: 20, color: '#333333' }}>
            <li>
              Clone or visit the official project on the <b>main branch</b>:{' '}
              <a href={mainBranchUrl} target="_blank" rel="noreferrer" style={{ color: '#006600', fontWeight: 'bold' }}>
                {repoUrl}
              </a>
            </li>
            <li>
              Download the APK build package from the{' '}
              <a href={releasesUrl} target="_blank" rel="noreferrer" style={{ color: '#006600', fontWeight: 'bold' }}>
                Releases Section
              </a>.
            </li>
            <li>Enable <i>&quot;Install from Unknown Sources&quot;</i> in your Android security settings.</li>
            <li>Launch the APK and grant storage permission to load your game assets.</li>
            <li>Adjust graphic resolution and control layout to match your device.</li>
          </ol>
        </div>

        <div style={{ marginTop: 18, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <a
            href="#back"
            onClick={(e) => {
              e.preventDefault();
              onNavigate('home');
            }}
            style={{ color: '#000000', fontWeight: 'bold', fontSize: 10 }}
          >
            &lt;&lt; Return to Home
          </a>
          <a
            href={mainBranchUrl}
            target="_blank"
            rel="noreferrer"
            style={{ color: '#006600', fontWeight: 'bold', fontSize: 10 }}
          >
            Open main branch on GitHub &gt;&gt;
          </a>
        </div>
      </div>
    </div>
  );
}
