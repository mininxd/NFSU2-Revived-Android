import React, { useState } from 'react';
import TopBanner from './components/TopBanner';
import NFSU2Nav from './components/NFSU2Nav';
import LeftNav from './components/LeftNav';
import HomePage from './components/HomePage';
import FeaturesPage from './components/FeaturesPage';
import ScreenshotsPage from './components/ScreenshotsPage';
import CityLifePage from './components/CityLifePage';
import AndroidPortPage from './components/AndroidPortPage';
import NewsPage from './components/NewsPage';
import LinksPage from './components/LinksPage';
import Footer from './components/Footer';
import { StoreFinderModal, LoginModal, ImageModal, NewsletterModal } from './components/Modals';

export default function App() {
  const [activePage, setActivePage] = useState('home');
  const [edition, setEdition] = useState('dec2004');

  const [storeFinderOpen, setStoreFinderOpen] = useState(false);
  const [loginOpen, setLoginOpen] = useState(false);
  const [newsletterOpen, setNewsletterOpen] = useState(false);
  const [modalImage, setModalImage] = useState(null);

  const handleNavigate = (page) => {
    setActivePage(page);
    window.scrollTo({ top: 0, behavior: 'smooth' });
  };

  const handleOpenLegal = (type) => {
    alert(`Need for Speed Underground 2 Legal Notice: ${type.toUpperCase()}\n\nNot affiliated with or endorsed by Electronic Arts.`);
  };

  return (
    <div style={{ backgroundColor: '#000000', minHeight: '100vh', paddingBottom: 40 }}>
      <div style={{ textAlign: 'center', paddingTop: 10 }}>
        <TopBanner onNavigate={handleNavigate} />
        <NFSU2Nav activePage={activePage} onNavigate={handleNavigate} />

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
              <td width="150" valign="top" className="leftnav-td">
                <LeftNav
                  activePage={activePage}
                  onNavigate={handleNavigate}
                  edition={edition}
                  onOpenNewsletter={() => setNewsletterOpen(true)}
                  onOpenPreorder={() => setStoreFinderOpen(true)}
                />
              </td>

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
                        src: './assets/images.ea.com/eagames/official/nfs/underground2/us/home/vinylcontest_125x72.jpg'
                      })
                    }
                  />
                )}
                {activePage === 'features' && <FeaturesPage onNavigate={handleNavigate} />}
                {activePage === 'screenshots' && (
                  <ScreenshotsPage onNavigate={handleNavigate} onOpenImageModal={setModalImage} />
                )}
                {activePage === 'citylife' && <CityLifePage onNavigate={handleNavigate} />}
                {activePage === 'android' && <AndroidPortPage onNavigate={handleNavigate} />}
                {activePage === 'news' && <NewsPage onNavigate={handleNavigate} />}
                {activePage === 'links' && <LinksPage onNavigate={handleNavigate} />}
                {activePage === 'videos' && (
                  <div style={{ width: 615, background: '#d8d8d8', padding: 20 }}>
                    <b style={{ fontSize: 12 }}>NEED FOR SPEED UNDERGROUND 2 - TRAILERS &amp; VIDEOS</b>
                    <p style={{ marginTop: 10, fontSize: 10, lineHeight: 1.5 }}>
                      Official E3 Trailer, City of Bayview Tour, and Tuner Playground videos (Historical archive).
                    </p>
                    <div style={{ marginTop: 16 }}>
                      <a href="#back" onClick={() => handleNavigate('home')} style={{ color: '#000000', fontWeight: 'bold' }}>
                        &lt;&lt; Return to Home
                      </a>
                    </div>
                  </div>
                )}
                {activePage === 'online' && (
                  <div style={{ width: 615, background: '#d8d8d8', minHeight: 520, fontFamily: 'verdana, arial, sans-serif' }}>
                    <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
                      <img
                        src="./assets/images.ea.com/eagames/official/nfs/underground2/us/universal/hdr_features.jpg"
                        width="615"
                        alt="Online Racing"
                        style={{ display: 'block' }}
                      />
                    </div>

                    <div style={{ padding: '12px 18px', background: '#1c261c', borderBottom: '2px solid #a1d464', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                      <div>
                        <span style={{ color: '#00ff66', fontSize: 10, fontWeight: 'bold' }}>&#9679; ONLINE MULTIPLAYER REVIVAL</span>
                        <div style={{ fontFamily: 'Impact, Arial Black', fontStyle: 'italic', fontSize: 18, color: '#ffffff', letterSpacing: 1 }}>
                          ONLINE RACING &amp; MULTIPLAYER LOBBIES
                        </div>
                      </div>
                      <div style={{ textAlign: 'right' }}>
                        <span style={{ background: '#00ff66', color: '#000000', fontSize: 9, fontWeight: 'bold', padding: '2px 6px', borderRadius: 2 }}>
                          SERVERS ACTIVE
                        </span>
                      </div>
                    </div>

                    <div style={{ padding: '16px 20px' }}>
                      <div
                        style={{
                          background: 'linear-gradient(135deg, #1b261b 0%, #0d160d 70%, #050a05 100%)',
                          border: '2px solid #3d593d',
                          borderRadius: 3,
                          padding: 18,
                          color: '#ffffff',
                          boxShadow: '0 4px 15px rgba(0,0,0,0.5)',
                          textAlign: 'center'
                        }}
                      >
                        <div style={{ fontSize: 9, color: '#a1d464', fontWeight: 'bold', textTransform: 'uppercase', letterSpacing: 1 }}>
                          COMMUNITY MULTIPLAYER SERVER
                        </div>
                        <div style={{ fontFamily: 'Impact, Arial Black', fontSize: 24, color: '#ffffff', fontStyle: 'italic', margin: '4px 0 10px 0', letterSpacing: 1 }}>
                          PLAY NEED FOR SPEED UNDERGROUND 2 ONLINE
                        </div>
                        <p style={{ fontSize: 10, lineHeight: 1.6, color: '#d0dfd0', maxWidth: 480, margin: '0 auto 16px auto' }}>
                          Compete online in Circuit, Sprint, Drift, Drag, and Street X battles with racers from around the globe on the active community revival server.
                        </p>

                        <a
                          href="https://nfsu2.xyz"
                          target="_blank"
                          rel="noopener noreferrer"
                          style={{
                            display: 'inline-flex',
                            alignItems: 'center',
                            gap: 8,
                            background: 'linear-gradient(90deg, #a1d464 0%, #00ff66 100%)',
                            color: '#000000',
                            textDecoration: 'none',
                            fontFamily: 'Impact, Arial Black, sans-serif',
                            fontStyle: 'italic',
                            fontSize: 16,
                            letterSpacing: 1.5,
                            padding: '10px 24px',
                            borderRadius: 3,
                            boxShadow: '0 0 16px rgba(0, 255, 100, 0.6)',
                            cursor: 'pointer'
                          }}
                          title="Launch NFSU2 Online on https://nfsu2.xyz"
                        >
                          <span style={{ fontSize: 14 }}>&#9658;</span> PLAY ON NFSU2.XYZ
                        </a>
                      </div>

                      <div style={{ marginTop: 18, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
                        <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
                          <b style={{ fontSize: 10, color: '#1a331a' }}>MULTIPLAYER GAME MODES</b>
                          <ul style={{ margin: '6px 0 0 0', paddingLeft: 16, fontSize: 9, color: '#444444', lineHeight: 1.5 }}>
                            <li><b>Circuit:</b> Multi-lap street circuits across Bayview</li>
                            <li><b>Sprint:</b> Point-to-point high-speed dashes</li>
                            <li><b>Drift:</b> Tandem and score-based downhill drift battles</li>
                            <li><b>Drag:</b> Manual shift quarter-mile drag strips</li>
                            <li><b>Street X:</b> Tight technical closed-track courses</li>
                          </ul>
                        </div>

                        <div style={{ background: '#e5e5e5', border: '1px solid #bbbbbb', padding: 10, borderRadius: 2 }}>
                          <b style={{ fontSize: 10, color: '#1a331a' }}>SERVER FEATURES</b>
                          <ul style={{ margin: '6px 0 0 0', paddingLeft: 16, fontSize: 9, color: '#444444', lineHeight: 1.5 }}>
                            <li>Cross-platform lobby matchmaking</li>
                            <li>Custom vehicle liveries &amp; performance tuning</li>
                            <li>Low-latency network netcode</li>
                            <li>Global leaderboards &amp; lap time rankings</li>
                            <li>Outrun free-roam challenges in Bayview</li>
                          </ul>
                        </div>
                      </div>

                      <div style={{ marginTop: 18, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                        <a
                          href="#back"
                          onClick={(e) => {
                            e.preventDefault();
                            handleNavigate('home');
                          }}
                          style={{ color: '#000000', fontWeight: 'bold', fontSize: 10 }}
                        >
                          &lt;&lt; Return to Home
                        </a>
                        <a
                          href="https://nfsu2.xyz"
                          target="_blank"
                          rel="noopener noreferrer"
                          style={{ color: '#006600', fontWeight: 'bold', fontSize: 10 }}
                        >
                          Open nfsu2.xyz directly &gt;&gt;
                        </a>
                      </div>
                    </div>
                  </div>
                )}
              </td>
            </tr>
          </tbody>
        </table>

        <Footer edition={edition} onSelectEdition={setEdition} onOpenLegal={handleOpenLegal} />
      </div>

      <StoreFinderModal isOpen={storeFinderOpen} onClose={() => setStoreFinderOpen(false)} />
      <LoginModal isOpen={loginOpen} onClose={() => setLoginOpen(false)} />
      <NewsletterModal isOpen={newsletterOpen} onClose={() => setNewsletterOpen(false)} />
      <ImageModal image={modalImage} onClose={() => setModalImage(null)} />
    </div>
  );
}
