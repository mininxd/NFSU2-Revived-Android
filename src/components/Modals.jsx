import React, { useState } from 'react';

export function StoreFinderModal({ isOpen, onClose }) {
  const [zip, setZip] = useState('');
  const [results, setResults] = useState(null);

  if (!isOpen) return null;

  const handleSearch = (e) => {
    e.preventDefault();
    if (!zip.trim()) return;
    setResults([
      { name: 'EB Games - Bayview Mall', dist: '1.2 miles', address: '402 Skyline Blvd', stock: 'In Stock' },
      { name: 'GameStop - Westside Plaza', dist: '2.5 miles', address: '1280 Beacon Ave', stock: 'In Stock' },
      { name: 'Best Buy - South City Center', dist: '4.1 miles', address: '775 Commerce St', stock: 'In Stock' },
      { name: 'Target - Jackson Heights', dist: '5.8 miles', address: '910 Industrial Pkwy', stock: 'Pre-Order Ready' }
    ]);
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-window"
        style={{ width: 340, background: '#333333', border: '1px solid #777777' }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', borderBottom: '1px solid #555555', paddingBottom: 6 }}>
          <b style={{ color: '#ffffff', fontSize: 10 }}>EA STORE LOCATOR (2004)</b>
          <span style={{ cursor: 'pointer', color: '#ffcc00' }} onClick={onClose}>
            [X]
          </span>
        </div>

        <div style={{ textAlign: 'center', margin: '12px 0 8px 0', fontSize: 10, color: '#ffffff' }}>
          ENTER YOUR POSTAL CODE TO FIND A LOCAL RETAILER:
        </div>

        <form onSubmit={handleSearch} style={{ textAlign: 'center', marginBottom: 10 }}>
          <input
            type="text"
            value={zip}
            onChange={(e) => setZip(e.target.value)}
            placeholder="e.g. 90210"
            style={{
              fontSize: 10,
              fontFamily: 'verdana',
              padding: '2px 4px',
              width: 100,
              marginRight: 6
            }}
          />
          <button
            type="submit"
            style={{
              fontFamily: 'verdana',
              fontSize: 9,
              fontWeight: 'bold',
              background: '#a1d464',
              color: '#000000',
              border: '1px solid #333333',
              cursor: 'pointer',
              padding: '2px 8px'
            }}
          >
            Find
          </button>
        </form>

        {results && (
          <div style={{ marginTop: 10, maxHeight: 180, overflowY: 'auto', background: '#222222', padding: 8, fontSize: 9 }}>
            <div style={{ color: '#a1d464', fontWeight: 'bold', marginBottom: 6 }}>
              Local Retailers within 15 miles of {zip}:
            </div>
            {results.map((r, i) => (
              <div key={i} style={{ borderBottom: '1px solid #444444', padding: '4px 0' }}>
                <b style={{ color: '#ffffff' }}>{r.name}</b> ({r.dist})<br />
                <span style={{ color: '#aaaaaa' }}>{r.address}</span> —{' '}
                <span style={{ color: '#00ff00' }}>{r.stock}</span>
              </div>
            ))}
          </div>
        )}

        <div style={{ textAlign: 'right', marginTop: 12 }}>
          <a href="#close" onClick={onClose} style={{ color: '#ffffff', textDecoration: 'underline', fontSize: 9 }}>
            CLOSE
          </a>
        </div>
      </div>
    </div>
  );
}

export function LoginModal({ isOpen, onClose }) {
  const [user, setUser] = useState('');
  const [pass, setPass] = useState('');
  const [loggedIn, setLoggedIn] = useState(false);

  if (!isOpen) return null;

  const handleLogin = (e) => {
    e.preventDefault();
    if (user) {
      setLoggedIn(true);
    }
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-window"
        style={{ width: 320, background: '#2e2e2e', border: '2px solid #555555' }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', borderBottom: '1px solid #444444', paddingBottom: 6 }}>
          <b style={{ color: '#ffcc00', fontSize: 10 }}>EA MEMBERS LOGIN</b>
          <span style={{ cursor: 'pointer', color: '#ffffff' }} onClick={onClose}>
            [X]
          </span>
        </div>

        {loggedIn ? (
          <div style={{ padding: '16px 0', textAlign: 'center' }}>
            <div style={{ color: '#00ff00', fontWeight: 'bold', marginBottom: 8 }}>
              Welcome back, {user}!
            </div>
            <div style={{ fontSize: 9, color: '#cccccc' }}>
              Logged into EA Online &amp; Need for Speed Underground 2 Network.
            </div>
            <button
              onClick={onClose}
              style={{
                marginTop: 14,
                fontFamily: 'verdana',
                fontSize: 9,
                background: '#a1d464',
                border: '1px solid #333333',
                padding: '3px 10px',
                cursor: 'pointer'
              }}
            >
              Continue
            </button>
          </div>
        ) : (
          <form onSubmit={handleLogin} style={{ padding: '12px 0' }}>
            <div style={{ marginBottom: 8, fontSize: 9 }}>
              <label>EA Member Name or Email:</label>
              <input
                type="text"
                value={user}
                onChange={(e) => setUser(e.target.value)}
                placeholder="RacerX_2004"
                style={{ width: '100%', marginTop: 2, padding: 3, fontSize: 10 }}
              />
            </div>
            <div style={{ marginBottom: 12, fontSize: 9 }}>
              <label>Password:</label>
              <input
                type="password"
                value={pass}
                onChange={(e) => setPass(e.target.value)}
                placeholder="••••••••"
                style={{ width: '100%', marginTop: 2, padding: 3, fontSize: 10 }}
              />
            </div>
            <div style={{ textAlign: 'right' }}>
              <button
                type="submit"
                style={{
                  fontFamily: 'verdana',
                  fontSize: 9,
                  fontWeight: 'bold',
                  background: '#a1d464',
                  color: '#000000',
                  border: '1px solid #333333',
                  padding: '3px 10px',
                  cursor: 'pointer'
                }}
              >
                Log In
              </button>
            </div>
          </form>
        )}
      </div>
    </div>
  );
}

export function ImageModal({ image, onClose }) {
  if (!image) return null;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        style={{
          background: '#111111',
          border: '2px solid #666666',
          boxShadow: '0 0 30px rgba(0,0,0,0.9)',
          padding: 8,
          maxWidth: '90vw',
          maxHeight: '90vh',
          display: 'flex',
          flexDirection: 'column'
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            alignItems: 'center',
            marginBottom: 6,
            color: '#ffffff',
            fontFamily: 'verdana',
            fontSize: 10,
            borderBottom: '1px solid #333333',
            paddingBottom: 4
          }}
        >
          <b>{image.title || 'EA GAMES IMAGE VIEWER'}</b>
          <button
            onClick={onClose}
            style={{
              background: '#333333',
              color: '#ffffff',
              border: '1px solid #777777',
              cursor: 'pointer',
              fontSize: 9,
              padding: '1px 6px'
            }}
          >
            Close [X]
          </button>
        </div>
        <div style={{ overflow: 'auto', textAlign: 'center' }}>
          <img
            src={image.src}
            alt={image.title || ''}
            style={{ maxWidth: '100%', maxHeight: '78vh', display: 'block', margin: '0 auto' }}
          />
        </div>
      </div>
    </div>
  );
}

export function NewsletterModal({ isOpen, onClose }) {
  const [email, setEmail] = useState('');
  const [signedUp, setSignedUp] = useState(false);

  if (!isOpen) return null;

  const handleSubmit = (e) => {
    e.preventDefault();
    if (email) setSignedUp(true);
  };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-window"
        style={{ width: 340, background: '#252525', border: '1px solid #555555' }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', borderBottom: '1px solid #444444', paddingBottom: 6 }}>
          <b style={{ color: '#a1d464', fontSize: 10 }}>THE ROAD REPORT NEWSLETTER</b>
          <span style={{ cursor: 'pointer', color: '#ffffff' }} onClick={onClose}>
            [X]
          </span>
        </div>

        {signedUp ? (
          <div style={{ padding: '16px 0', textAlign: 'center' }}>
            <div style={{ color: '#00ff00', fontWeight: 'bold' }}>Thank You for Subscribing!</div>
            <div style={{ fontSize: 9, color: '#cccccc', marginTop: 6 }}>
              You will receive exclusive Need for Speed Underground 2 news, desktop wallpapers, and insider reports at{' '}
              <b>{email}</b>.
            </div>
            <button
              onClick={onClose}
              style={{
                marginTop: 12,
                fontFamily: 'verdana',
                fontSize: 9,
                background: '#a1d464',
                padding: '2px 8px',
                cursor: 'pointer'
              }}
            >
              Done
            </button>
          </div>
        ) : (
          <form onSubmit={handleSubmit} style={{ padding: '10px 0' }}>
            <div style={{ fontSize: 9, color: '#dddddd', marginBottom: 10, lineHeight: 1.4 }}>
              Sign up for the official <i>Road Report</i> newsletter to receive exclusive concept artwork, gameplay
              previews, and development updates directly from the EA Vancouver team.
            </div>
            <div style={{ marginBottom: 10 }}>
              <label style={{ fontSize: 9, color: '#aaaaaa' }}>Email Address:</label>
              <input
                type="email"
                required
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                placeholder="tuner@example.com"
                style={{ width: '100%', marginTop: 2, padding: 3, fontSize: 10 }}
              />
            </div>
            <div style={{ textAlign: 'right' }}>
              <button
                type="submit"
                style={{
                  fontFamily: 'verdana',
                  fontSize: 9,
                  fontWeight: 'bold',
                  background: '#a1d464',
                  color: '#000000',
                  border: '1px solid #333333',
                  padding: '3px 10px',
                  cursor: 'pointer'
                }}
              >
                Sign Up
              </button>
            </div>
          </form>
        )}
      </div>
    </div>
  );
}
