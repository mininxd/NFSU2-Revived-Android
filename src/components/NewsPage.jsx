import React from 'react';

const NEWS_ARTICLES = [
  {
    date: '11.15.04',
    title: 'Need for Speed Underground 2 Ships Worldwide!',
    body: 'Electronic Arts announced that Need for Speed Underground 2 has officially shipped to retailers nationwide for PlayStation 2, Xbox, Nintendo GameCube, PC CD-ROM, and Game Boy Advance. Experience free-roaming exploration through Bayview city, over 30 licensed tuner vehicles, and billions of visual customizations.'
  },
  {
    date: '11.02.04',
    title: 'EA Trax Soundtrack Revealed - Snoop Dogg & The Doors Headlining',
    body: 'The complete EA Trax lineup for Need for Speed Underground 2 has been unveiled, featuring 27 tracks of adrenaline-pumping hip hop, rock, and electronic music. The centerpiece is Snoop Dogg featuring The Doors with the exclusive remix of "Riders on the Storm" produced by Fredwreck.'
  },
  {
    date: '10.28.04',
    title: 'Official PC Demo Now Available',
    body: 'Get your first taste of Bayview! The official Need for Speed Underground 2 single-player PC demo is now available for download. Take the Nissan 350Z through Circuit and Downhill Drift events and customize your ride at the local Car Lot and Performance Shop.'
  },
  {
    date: '08.12.04',
    title: 'Brooke Burke Signs on as In-Game Mentor Rachel Teller',
    body: 'TV personality and model Brooke Burke has joined the cast of Need for Speed Underground 2 as Rachel Teller. Rachel will serve as your trusted ally and mentor throughout your journey to become the top racer in Bayview.'
  },
  {
    date: '07.06.04',
    title: 'Four New Underground 2 Screenshots Released',
    body: 'Check out the Screenshots section for four brand new high-resolution captures showing off the nighttime lighting engine, reflection mapping, and licensed body kit customization on the Nissan 350Z.'
  },
  {
    date: '06.30.04',
    title: 'Custom Car Vinyl Contest Opens',
    body: 'Submit your best custom vinyl designs to EA Games for a chance to have your graphic included directly on cars in the final release of Need for Speed Underground 2!'
  }
];

export default function NewsPage({ onNavigate }) {
  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 520 }}>
      {/* Header */}
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="/assets/images.ea.com/eagames/official/nfs/underground2/us/universal/features_news.jpg"
          width="615"
          alt="News Archives"
          style={{ display: 'block' }}
        />
      </div>

      <div style={{ padding: '16px 20px', fontFamily: 'verdana, arial, sans-serif', fontSize: 10 }}>
        <div style={{ fontWeight: 'bold', fontSize: 12, borderBottom: '1px solid #bbbbbb', paddingBottom: 6, marginBottom: 14 }}>
          LATEST UNDERGROUND 2 NEWS &amp; ANNOUNCEMENTS
        </div>

        {NEWS_ARTICLES.map((item, idx) => (
          <div
            key={idx}
            style={{
              marginBottom: 16,
              paddingBottom: 12,
              borderBottom: '1px solid #cccccc'
            }}
          >
            <span style={{ color: '#006600', fontWeight: 'bold' }}>{item.date}</span> —{' '}
            <span style={{ fontWeight: 'bold', fontSize: 11, color: '#111111' }}>{item.title}</span>
            <div style={{ marginTop: 6, lineHeight: 1.45, color: '#333333' }}>{item.body}</div>
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
