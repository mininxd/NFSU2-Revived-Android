import React, { useState } from 'react';
import NetworkNav from './components/NetworkNav';
import TopBanner from './components/TopBanner';
import GameFinderBar from './components/GameFinderBar';
import LeftNav from './components/LeftNav';
import HomePage from './components/HomePage';
import FeaturesPage from './components/FeaturesPage';
import ScreenshotsPage from './components/ScreenshotsPage';
import NewsPage from './components/NewsPage';
import LinksPage from './components/LinksPage';
import Footer from './components/Footer';
import { StoreFinderModal, LoginModal, ImageModal, NewsletterModal } from './components/Modals';

export default function App() {
  const [activePage, setActivePage] = useState('home');
  const [edition, setEdition] = useState('july2004'); // 'july2004' or 'dec2004'
  const [currentRegion, setCurrentRegion] = useState('us');

  // Modals state
  const [storeFinderOpen, setStoreFinderOpen] = useState(false);
  const [loginOpen, setLoginOpen] = useState(false);
  const [newsletterOpen, setNewsletterOpen] = useState(false);
  const [modalImage, setModalImage] = useState(null);

  const handleNavigate = (page) => {
    setActivePage(page);
    window.scrollTo({ top: 0, behavior: 'smooth' });
  };

  const handleSelectGame = (gameName) => {
    if (gameName.includes('Underground 2')) {
      setActivePage('home');
    } else {
      alert(`EA Games Archive: Navigating to ${gameName} official page (2004 historical index)`);
    }
  };

  const handleSelectRegion = (regionCode) => {
    setCurrentRegion(regionCode);
    alert(`Region changed to: ${regionCode.toUpperCase()} (http://www.eagames.com/official/nfs/underground2/${regionCode}/)`);
  };

  const handleOpenSearch = (term) => {
    alert(`EA Games Search: Querying archive for "${term}"`);
  };

  const handleOpenLegal = (type) => {
    alert(`Electronic Arts Legal Notice: ${type.toUpperCase()} (Official 2004 archive policy)`);
  };

  return (
    <div style={{ backgroundColor: '#000000', minHeight: '100vh', paddingBottom: 40 }}>
      {/* Historical Era Switcher Banner */}
      <div
        style={{
          background: '#151515',
          borderBottom: '1px solid #2a2a2a',
          padding: '4px 12px',
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          color: '#888888',
          fontSize: 10,
          fontFamily: 'verdana, sans-serif'
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{ color: '#00ff66', fontWeight: 'bold' }}>&#9679; ARCHIVE ACTIVE</span>
          <span>Wayback Machine Recreation: <b>eagames.com/official/nfs/underground2/us/home.jsp</b></span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span>Switch Edition:</span>
          <button
            onClick={() => setEdition('july2004')}
            style={{
              background: edition === 'july2004' ? '#a1d464' : '#252525',
              color: edition === 'july2004' ? '#000000' : '#cccccc',
              border: '1px solid #444444',
              fontFamily: 'verdana',
              fontSize: 9,
              fontWeight: edition === 'july2004' ? 'bold' : 'normal',
              padding: '2px 8px',
              cursor: 'pointer'
            }}
          >
            July 2004 (Pre-Release)
          </button>
          <button
            onClick={() => setEdition('dec2004')}
            style={{
              background: edition === 'dec2004' ? '#a1d464' : '#252525',
              color: edition === 'dec2004' ? '#000000' : '#cccccc',
              border: '1px solid #444444',
              fontFamily: 'verdana',
              fontSize: 9,
              fontWeight: edition === 'dec2004' ? 'bold' : 'normal',
              padding: '2px 8px',
              cursor: 'pointer'
            }}
          >
            Dec 2004 (Official Launch)
          </button>
        </div>
      </div>

      {/* Top EA Network Uninav */}
      <NetworkNav
        onOpenStoreFinder={() => setStoreFinderOpen(true)}
        onOpenLogin={() => setLoginOpen(true)}
        onOpenSupport={() => alert('EA Customer Support: Knowledge base & troubleshooting for Need for Speed')}
      />

      {/* Center 765px Stage */}
      <div style={{ textAlign: 'center' }}>
        {/* Top Banner (765x76) */}
        <TopBanner onNavigate={handleNavigate} />

        {/* Game Finder Gray Bar (765x25) */}
        <GameFinderBar
          onSelectGame={handleSelectGame}
          onSelectRegion={handleSelectRegion}
          currentRegion={currentRegion}
          onOpenSearch={handleOpenSearch}
        />

        {/* 2-Column Main Table (765px width) */}
        <table
          cellPadding="0"
          cellSpacing="0"
          border="0"
          width="765"
          bgcolor="#d8d8d8"
          style={{ margin: '0 auto', textAlign: 'left' }}
        >
          <tbody>
            <tr>
              {/* Left Nav Column (150px) */}
              <td width="150" valign="top" className="leftnav-td">
                <LeftNav
                  activePage={activePage}
                  onNavigate={handleNavigate}
                  edition={edition}
                  onOpenNewsletter={() => setNewsletterOpen(true)}
                  onOpenPreorder={() => setStoreFinderOpen(true)}
                />
              </td>

              {/* Main Stage Column (615px) */}
              <td width="615" valign="top" className="mainstage-td">
                {activePage === 'home' && (
                  <HomePage
                    edition={edition}
                    onNavigate={handleNavigate}
                    onOpenImageModal={setModalImage}
                    onOpenNewsletter={() => setNewsletterOpen(true)}
                    onOpenCarContest={() =>
                      setModalImage({
                        title: 'Underground 2 Custom Car Contest Details',
                        src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/vinylcontest_125x72.jpg'
                      })
                    }
                  />
                )}
                {activePage === 'features' && <FeaturesPage onNavigate={handleNavigate} />}
                {activePage === 'screenshots' && (
                  <ScreenshotsPage onNavigate={handleNavigate} onOpenImageModal={setModalImage} />
                )}
                {activePage === 'news' && <NewsPage onNavigate={handleNavigate} />}
                {activePage === 'links' && <LinksPage onNavigate={handleNavigate} />}
                {activePage === 'videos' && (
                  <div style={{ width: 615, background: '#d8d8d8', padding: 20 }}>
                    <b style={{ fontSize: 12 }}>NEED FOR SPEED UNDERGROUND 2 - TRAILERS &amp; VIDEOS</b>
                    <p style={{ marginTop: 10, fontSize: 10 }}>
                      Official E3 Trailer, City of Bayview Tour, and Tuner Playground videos (Historical archive).
                    </p>
                    <div style={{ marginTop: 12 }}>
                      <a href="#back" onClick={() => handleNavigate('home')} style={{ color: '#000000', fontWeight: 'bold' }}>
                        &lt;&lt; Return to Home
                      </a>
                    </div>
                  </div>
                )}
                {activePage === 'citylife' && (
                  <div style={{ width: 615, background: '#d8d8d8', padding: 20 }}>
                    <b style={{ fontSize: 12 }}>EXPLORE BAYVIEW CITY LIFE</b>
                    <p style={{ marginTop: 10, fontSize: 10 }}>
                      Cruise freely through City Center, Beacon Hill, Jackson Heights, Coal Harbour, and the Airport.
                    </p>
                    <div style={{ marginTop: 12 }}>
                      <a href="#back" onClick={() => handleNavigate('home')} style={{ color: '#000000', fontWeight: 'bold' }}>
                        &lt;&lt; Return to Home
                      </a>
                    </div>
                  </div>
                )}
                {activePage === 'online' && (
                  <div style={{ width: 615, background: '#d8d8d8', padding: 20 }}>
                    <b style={{ fontSize: 12 }}>ONLINE RACING COMMUNITY &amp; LOBBIES</b>
                    <p style={{ marginTop: 10, fontSize: 10 }}>
                      Compete online in Sprint, Circuit, Drift, and Outrun battles with racers from around the globe.
                    </p>
                    <div style={{ marginTop: 12 }}>
                      <a href="#back" onClick={() => handleNavigate('home')} style={{ color: '#000000', fontWeight: 'bold' }}>
                        &lt;&lt; Return to Home
                      </a>
                    </div>
                  </div>
                )}
              </td>
            </tr>
          </tbody>
        </table>

        {/* Footer */}
        <Footer edition={edition} onOpenLegal={handleOpenLegal} />
      </div>

      {/* Modals */}
      <StoreFinderModal isOpen={storeFinderOpen} onClose={() => setStoreFinderOpen(false)} />
      <LoginModal isOpen={loginOpen} onClose={() => setLoginOpen(false)} />
      <NewsletterModal isOpen={newsletterOpen} onClose={() => setNewsletterOpen(false)} />
      <ImageModal image={modalImage} onClose={() => setModalImage(null)} />
    </div>
  );
}
