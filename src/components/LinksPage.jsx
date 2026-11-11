import React from 'react';

const COMMUNITY_LINKS = [
  {
    name: 'RacerPlanet.com',
    url: 'http://www.racerplanet.com',
    desc: 'News, reviews, car downloads and extensive discussion forums dedicated to arcade and simulation racing.'
  },
  {
    name: 'NFSUnlimited.net',
    url: 'http://www.nfsunlimited.net',
    desc: 'The definitive Need for Speed community resource featuring vinyl showrooms, tuning setups, and race leaderboards.'
  },
  {
    name: 'ND4SPDWorld.com',
    url: 'http://www.nd4spdworld.com',
    desc: 'In-depth coverage of the Need for Speed series, exclusive developer interviews, and patch mirrors.'
  },
  {
    name: 'NFSCars.net',
    url: 'http://www.nfscars.net',
    desc: 'Vast archive of car models, livery downloads, modding tools, and livery competitions.'
  },
  {
    name: 'Formula Drift (Formula D)',
    url: 'http://www.formulad.com',
    desc: 'Official partner of Need for Speed Underground 2 - Professional American drifting championship.'
  },
  {
    name: 'Electronic Arts Official Portal',
    url: 'http://www.ea.com',
    desc: 'Main portal for all EA Games, EA SPORTS, EA SPORTS BIG, and POGO titles.'
  }
];

export default function LinksPage({ onNavigate }) {
  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 520 }}>
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="./assets/images.ea.com/eagames/official/nfs/underground2/us/universal/hdr_links.jpg"
          width="615"
          alt="Links"
          style={{ display: 'block' }}
        />
      </div>

      <div style={{ padding: '16px 20px', fontFamily: 'verdana, arial, sans-serif', fontSize: 10 }}>
        <div style={{ fontWeight: 'bold', fontSize: 12, borderBottom: '1px solid #bbbbbb', paddingBottom: 6, marginBottom: 14 }}>
          OFFICIAL &amp; COMMUNITY FAN LINKS
        </div>

        {COMMUNITY_LINKS.map((link, idx) => (
          <div
            key={idx}
            style={{
              marginBottom: 14,
              paddingBottom: 10,
              borderBottom: '1px solid #cccccc'
            }}
          >
            <a
              href={link.url}
              target="_blank"
              rel="noreferrer"
              style={{ fontWeight: 'bold', fontSize: 11, color: '#003366', textDecoration: 'underline' }}
            >
              {link.name} &gt;&gt;
            </a>
            <div style={{ marginTop: 4, lineHeight: 1.4, color: '#333333' }}>{link.desc}</div>
          </div>
        ))}

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
