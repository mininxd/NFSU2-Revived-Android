import React from 'react';

export default function NFSU2Nav({ activePage, onNavigate }) {
  const navItems = [
    { key: 'home', label: 'HOME' },
    { key: 'features', label: 'FEATURES' },
    { key: 'screenshots', label: 'SCREENSHOTS' },
    { key: 'citylife', label: 'CITY LIFE' },
    { key: 'android', label: 'ANDROID PORT' },
    { key: 'online', label: 'ONLINE PLAY' },
    { key: 'news', label: 'NEWS' },
    { key: 'links', label: 'COMMUNITY' }
  ];

  return (
    <div
      style={{
        width: 765,
        height: 28,
        margin: '0 auto',
        background: 'linear-gradient(180deg, #2b332b 0%, #171f17 50%, #0d140d 100%)',
        borderBottom: '2px solid #a1d464',
        borderTop: '1px solid #333333',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        padding: '0 8px',
        boxShadow: '0 2px 8px rgba(0,0,0,0.6)'
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 5 }}>
        {navItems.map((item) => {
          const isActive = activePage === item.key;
          return (
            <button
              key={item.key}
              onClick={() => onNavigate(item.key)}
              style={{
                background: isActive ? '#a1d464' : 'transparent',
                color: isActive ? '#000000' : '#ffffff',
                border: 'none',
                fontFamily: 'Impact, Arial Black, sans-serif',
                fontStyle: 'italic',
                fontSize: 11,
                letterSpacing: 1,
                padding: '4px 9px',
                cursor: 'pointer',
                transition: 'all 0.15s ease',
                textShadow: isActive ? 'none' : '0 1px 2px #000'
              }}
              onMouseEnter={(e) => {
                if (!isActive) {
                  e.currentTarget.style.color = '#a1d464';
                }
              }}
              onMouseLeave={(e) => {
                if (!isActive) {
                  e.currentTarget.style.color = '#ffffff';
                }
              }}
            >
              {item.label}
            </button>
          );
        })}
      </div>
    </div>
  );
}
