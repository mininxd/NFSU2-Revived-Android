import React, { useState } from 'react';

const SCREENSHOT_SETS = {
  smooth_ride: {
    title: 'Smooth Ride',
    date: '07.06.04',
    screens: [
      {
        id: 1,
        thumb: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/320x240/smooth_ride/screen_1.jpg',
        full: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/800x600/smooth_ride/screen_1.jpg',
        desc: 'Nissan 350Z cruising past city streetlights'
      },
      {
        id: 2,
        thumb: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/320x240/smooth_ride/screen_2.jpg',
        full: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/800x600/smooth_ride/screen_2.jpg',
        desc: 'Custom tuned tuner taking high speed corner'
      },
      {
        id: 3,
        thumb: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/320x240/smooth_ride/screen_3.jpg',
        full: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/800x600/smooth_ride/screen_3.jpg',
        desc: 'Neon glow reflection and nighttime atmosphere'
      },
      {
        id: 4,
        thumb: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/320x240/smooth_ride/screen_4.jpg',
        full: '/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/800x600/smooth_ride/screen_4.jpg',
        desc: 'Bayview highway drag stretch'
      }
    ]
  }
};

export default function ScreenshotsPage({ onNavigate, onOpenImageModal }) {
  const [currentSetKey, setCurrentSetKey] = useState('smooth_ride');
  const [currentIndex, setCurrentIndex] = useState(0);

  const currentSet = SCREENSHOT_SETS[currentSetKey];
  const currentImg = currentSet.screens[currentIndex];

  const handlePrev = () => {
    setCurrentIndex((prev) => (prev > 0 ? prev - 1 : currentSet.screens.length - 1));
  };

  const handleNext = () => {
    setCurrentIndex((prev) => (prev < currentSet.screens.length - 1 ? prev + 1 : 0));
  };

  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 520 }}>
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/hdr_screenshots.jpg"
          width="615"
          alt="Screenshots"
          style={{ display: 'block' }}
        />
      </div>

      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          padding: '6px 14px',
          background: '#444444',
          color: '#ffffff',
          fontFamily: 'verdana',
          fontSize: 9
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <img
            src="/assets/images.ea.com/eagames/official/nfs/underground2/us/screenshots/hdr_ps2.gif"
            alt="PlayStation 2"
            style={{ height: 16 }}
          />
          <b>SET: {currentSet.title}</b>
        </div>
        <div>
          <span>DATE: {currentSet.date}</span>
          <span style={{ marginLeft: 12 }}>
            IMAGE <b>{currentIndex + 1}</b> OF <b>{currentSet.screens.length}</b>
          </span>
        </div>
      </div>

      <div style={{ padding: '15px 0', textAlign: 'center', background: '#222222' }}>
        <div style={{ display: 'inline-block', position: 'relative' }}>
          <img
            src={currentImg.thumb}
            width="320"
            height="240"
            alt={currentImg.desc}
            style={{
              border: '2px solid #555555',
              cursor: 'pointer',
              display: 'block'
            }}
            onClick={() =>
              onOpenImageModal({
                title: `${currentSet.title} - Screenshot ${currentIndex + 1} (800x600)`,
                src: currentImg.full
              })
            }
            title="Click to open full 800x600 resolution"
          />

          <div
            style={{
              marginTop: 6,
              color: '#cccccc',
              fontSize: 9,
              fontFamily: 'verdana'
            }}
          >
            {currentImg.desc} —{' '}
            <span
              style={{ color: '#a1d464', cursor: 'pointer', textDecoration: 'underline' }}
              onClick={() =>
                onOpenImageModal({
                  title: `${currentSet.title} - Screenshot ${currentIndex + 1} (800x600)`,
                  src: currentImg.full
                })
              }
            >
              Click for 800x600 Full Screen
            </span>
          </div>

          <div
            style={{
              display: 'flex',
              justifyContent: 'center',
              alignItems: 'center',
              gap: 12,
              marginTop: 10
            }}
          >
            <button
              onClick={handlePrev}
              style={{
                fontFamily: 'verdana',
                fontSize: 9,
                fontWeight: 'bold',
                background: '#333333',
                color: '#ffffff',
                border: '1px solid #777777',
                padding: '3px 10px',
                cursor: 'pointer'
              }}
            >
              &lt;&lt; PREV
            </button>
            <span style={{ color: '#ffffff', fontSize: 9 }}>
              {currentIndex + 1} / {currentSet.screens.length}
            </span>
            <button
              onClick={handleNext}
              style={{
                fontFamily: 'verdana',
                fontSize: 9,
                fontWeight: 'bold',
                background: '#333333',
                color: '#ffffff',
                border: '1px solid #777777',
                padding: '3px 10px',
                cursor: 'pointer'
              }}
            >
              NEXT &gt;&gt;
            </button>
          </div>
        </div>
      </div>

      <div style={{ padding: '14px 20px', fontFamily: 'verdana', fontSize: 10 }}>
        <div style={{ fontWeight: 'bold', marginBottom: 8, borderBottom: '1px solid #bbbbbb', paddingBottom: 4 }}>
          AVAILABLE SCREENSHOT SETS
        </div>

        <table cellPadding="4" cellSpacing="0" border="0" width="100%">
          <tbody>
            <tr style={{ background: '#e0e0e0' }}>
              <td>
                <span className="default">
                  <b>Smooth Ride</b> (4 Screens)
                </span>
              </td>
              <td align="right">
                <span className="default">07.06.04</span>
              </td>
            </tr>
            <tr>
              <td>
                <span className="default">
                  <a
                    href="#screens"
                    onClick={(e) => {
                      e.preventDefault();
                      setCurrentIndex(1);
                    }}
                    style={{ color: '#000000' }}
                  >
                    True Velocity
                  </a>
                </span>
              </td>
              <td align="right">
                <span className="default">06.29.04</span>
              </td>
            </tr>
            <tr style={{ background: '#e0e0e0' }}>
              <td>
                <span className="default">
                  <a
                    href="#screens"
                    onClick={(e) => {
                      e.preventDefault();
                      setCurrentIndex(2);
                    }}
                    style={{ color: '#000000' }}
                  >
                    Crusin
                  </a>
                </span>
              </td>
              <td align="right">
                <span className="default">06.18.04</span>
              </td>
            </tr>
            <tr>
              <td>
                <span className="default">
                  <a
                    href="#screens"
                    onClick={(e) => {
                      e.preventDefault();
                      setCurrentIndex(3);
                    }}
                    style={{ color: '#000000' }}
                  >
                    Street Wise
                  </a>
                </span>
              </td>
              <td align="right">
                <span className="default">06.18.04</span>
              </td>
            </tr>
          </tbody>
        </table>

        <div style={{ marginTop: 14 }}>
          <a
            href="#back"
            onClick={(e) => {
              e.preventDefault();
              onNavigate('home');
            }}
            style={{ color: '#000000', fontWeight: 'bold' }}
          >
            &lt;&lt; Return to Home
          </a>
        </div>
      </div>
    </div>
  );
}
