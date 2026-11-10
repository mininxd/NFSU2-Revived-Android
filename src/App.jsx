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
import EATraxModal from './components/EATraxModal';
import { StoreFinderModal, LoginModal, ImageModal, NewsletterModal } from './components/Modals';

export default function App() {
  const [activePage, setActivePage] = useState('home');
  const [edition, setEdition] = useState('dec2004');

  const [storeFinderOpen, setStoreFinderOpen] = useState(false);
  const [loginOpen, setLoginOpen] = useState(false);
  const [newsletterOpen, setNewsletterOpen] = useState(false);
  const [eaTraxOpen, setEaTraxOpen] = useState(false);
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
        <NFSU2Nav
          activePage={activePage}
          onNavigate={handleNavigate}
          onOpenEATrax={() => setEaTraxOpen(true)}
        />

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
                  onOpenEATrax={() => setEaTraxOpen(true)}
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
                        src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/vinylcontest_125x72.jpg'
                      })
                    }
                    onOpenEATrax={() => setEaTraxOpen(true)}
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
                  <div style={{ width: 615, background: '#d8d8d8', padding: 20 }}>
                    <b style={{ fontSize: 12 }}>ONLINE RACING &amp; MULTIPLAYER</b>
                    <p style={{ marginTop: 10, fontSize: 10, lineHeight: 1.5 }}>
                      Compete online in Circuit, Sprint, Drift, Drag, and Street X competitions with players worldwide. Tune your custom ride to perfection and test your skills in online matchmaking lobbies.
                    </p>
                    <div style={{ marginTop: 16 }}>
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

        <Footer edition={edition} onSelectEdition={setEdition} onOpenLegal={handleOpenLegal} />
      </div>

      <StoreFinderModal isOpen={storeFinderOpen} onClose={() => setStoreFinderOpen(false)} />
      <LoginModal isOpen={loginOpen} onClose={() => setLoginOpen(false)} />
      <NewsletterModal isOpen={newsletterOpen} onClose={() => setNewsletterOpen(false)} />
      <ImageModal image={modalImage} onClose={() => setModalImage(null)} />
      <EATraxModal isOpen={eaTraxOpen} onClose={() => setEaTraxOpen(false)} />
    </div>
  );
}
