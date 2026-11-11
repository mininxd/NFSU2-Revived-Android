import React, { useState } from 'react';

function LeftNavPlayBanner() {
  return (
    <a
      href="https://nfsu2.xyz"
      target="_blank"
      rel="noopener noreferrer"
      className="nfsu2-leftnav-play-banner"
      title="Play Need for Speed Underground 2 Online"
    >
      <div className="nfsu2-leftnav-play-badge">
        <span className="nfsu2-leftnav-play-dot" />
        <span>ONLINE REVIVAL</span>
      </div>
      <div className="nfsu2-leftnav-play-main">
        <span className="nfsu2-leftnav-play-arrow">&#9658;</span>
        <span>PLAY NOW</span>
      </div>
      <div className="nfsu2-leftnav-play-sub">NFSU2.XYZ</div>
    </a>
  );
}

function RolloverButton({ normalSrc, hoverSrc, alt, onClick, active }) {
  const [hovered, setHovered] = useState(false);
  const currentSrc = (hovered || active) && hoverSrc ? hoverSrc : normalSrc;

  return (
    <div
      style={{ cursor: onClick ? 'pointer' : 'default', lineHeight: 0 }}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      onClick={onClick}
    >
      <img
        src={currentSrc}
        alt={alt || ''}
        border="0"
        width="150"
        style={{ display: 'block' }}
      />
    </div>
  );
}

export default function LeftNav({ activePage, onNavigate, edition, onOpenNewsletter, onOpenPreorder }) {
  const isDecEdition = edition === 'dec2004';

  return (
    <div
      style={{
        width: 150,
        background: `url('./assets/www.ea.com/downloads/eagames/official/nfs/underground2/us/leftnav_background.jpg') repeat-y #e4e4e4`,
        minHeight: 620
      }}
    >
      {isDecEdition ? (
        <>
          <LeftNavPlayBanner />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/locator.jpg"
            alt="Retailer Locator"
            onClick={onOpenPreorder}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/game_info.gif"
            width="150"
            alt="Game Info"
            style={{ display: 'block' }}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/features.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/features_over.gif"
            alt="Features"
            active={activePage === 'features'}
            onClick={() => onNavigate('features')}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/screens.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/screens_over.gif"
            alt="Screenshots"
            active={activePage === 'screenshots'}
            onClick={() => onNavigate('screenshots')}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/video.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/video_over.gif"
            alt="Videos"
            active={activePage === 'videos'}
            onClick={() => onNavigate('videos')}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/hdr_go.gif"
            width="150"
            alt="Go Underground"
            style={{ display: 'block' }}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/city.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/city_over.gif"
            alt="City Life"
            active={activePage === 'citylife'}
            onClick={() => onNavigate('citylife')}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/soundtrack_gray.gif"
            width="150"
            alt="EA Trax Soundtrack"
            style={{ display: 'block' }}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/hdr_community.gif"
            width="150"
            alt="Community"
            style={{ display: 'block' }}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/onlineplay2.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/onlineplay-over2.gif"
            alt="Online Play"
            active={activePage === 'online'}
            onClick={() => onNavigate('online')}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/news2.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav2/news-over2.gif"
            alt="News"
            active={activePage === 'news'}
            onClick={() => onNavigate('news')}
          />
        </>
      ) : (
        <>
          <LeftNavPlayBanner />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/newsletter.jpg"
            alt="Newsletter"
            onClick={onOpenNewsletter}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_1.gif"
            width="150"
            height="29"
            alt="Game Info"
            style={{ display: 'block' }}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_2.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_2-over.gif"
            alt="Features"
            active={activePage === 'features'}
            onClick={() => onNavigate('features')}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_3.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_3-over.gif"
            alt="Screenshots"
            active={activePage === 'screenshots'}
            onClick={() => onNavigate('screenshots')}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_4.gif"
            width="150"
            height="21"
            alt=""
            style={{ display: 'block' }}
          />
          <img
            src="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_5.gif"
            width="150"
            height="26"
            alt="Community"
            style={{ display: 'block' }}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_6.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_6-over.gif"
            alt="News"
            active={activePage === 'news'}
            onClick={() => onNavigate('news')}
          />
          <RolloverButton
            normalSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_7.gif"
            hoverSrc="./assets/images.ea.com/eagames/official/nfs/underground2/us/leftnav/leftnav_7-over.gif"
            alt="Links"
            active={activePage === 'links'}
            onClick={() => onNavigate('links')}
          />
        </>
      )}

      <a href="http://www.formulad.com" target="_blank" rel="noreferrer" style={{ lineHeight: 0, display: 'block', marginTop: 4 }}>
        <img
          src="./assets/images.ea.com/eagames/official/nfs/underground2/us/home/promotion.jpg"
          border="0"
          alt="Formula D"
          width="150"
          style={{ display: 'block' }}
        />
      </a>

      <table
        width="150"
        cellPadding="10"
        cellSpacing="0"
        border="0"
        style={{
          background: `url('./assets/images.ea.com/eagames/official/nfs/underground2/us/home/platform.jpg') no-repeat`,
          backgroundSize: '150px auto'
        }}
      >
        <tbody>
          <tr>
            <td align="center" width="150">
              <span style={{ color: 'white', fontSize: '7pt', fontFamily: 'Verdana', lineHeight: 1.3 }}>
                Available for PlayStation&reg;2, Xbox&reg;, Nintendo GameCube&#153;, Game Boy&reg; Advance &amp; PC.
              </span>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
}
