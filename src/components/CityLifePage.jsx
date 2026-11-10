import React, { useState } from 'react';

const DISTRICTS = [
  {
    id: 'citycenter',
    name: 'City Center',
    subtitle: 'Downtown Urban Grid & Nightlife District',
    stage: 'Unlocked in Stage 1',
    description: 'The pulsing heart of Bayview. High-density skyscrapers, wide asphalt boulevards, neon-lit casino hotels, and technical 90-degree city street intersections.',
    races: ['Circuit', 'Sprint', 'Outrun Battles'],
    shops: [
      { type: 'Car Lot', color: '#ffb300', name: 'Downtown City Center Dealership', desc: 'Starting garage stock including Peugeot 206, Mazda Miata, Nissan 240SX' },
      { type: 'Body Shop', color: '#00ff66', name: 'South Center Body Works', desc: 'Front/rear bumpers, side skirts, spoilers, hood scoops' },
      { type: 'Performance', color: '#00bfff', name: 'Hotel Plaza Performance Tuning', desc: 'Engine packages, ECU re-mapping, street nitrous oxide systems' }
    ],
    difficulty: 'Moderate',
    landmarks: ['Bayview City Hall', 'The Grand Casino Strip', 'Hotel Plaza', 'Central Freeway Interchange'],
    accentColor: '#00ff66'
  },
  {
    id: 'beaconhill',
    name: 'Beacon Hill',
    subtitle: 'Winding Serpentine Hills & Luxury Overlooks',
    stage: 'Unlocked in Stage 2',
    description: 'Winding hillside roads overlooking the Bayview skyline. Tight switchbacks, sweeping elevation drops, and serpentine downhill passes ideal for high-speed technical drifting.',
    races: ['Downhill Drift', 'Sprint', 'Street X'],
    shops: [
      { type: 'Performance', color: '#00bfff', name: 'Upper Beacon Hill Tuning', desc: 'Stage 2 turbo upgrades, sport brake kits, performance tires' },
      { type: 'Graphics Shop', color: '#ff3366', name: 'West Ridge Vinyl & Decals', desc: 'Manufacturer vinyls, custom tribal and flame livery designs' }
    ],
    difficulty: 'Challenging',
    landmarks: ['Beacon Lookout Point', 'Hillside Serpentine Pass', 'West Ridge Estates'],
    accentColor: '#a1d464'
  },
  {
    id: 'jacksonheights',
    name: 'Jackson Heights',
    subtitle: 'High-Altitude Mountain Ridge & Radio Tower',
    stage: 'Unlocked in Stage 3',
    description: 'The highest elevation in Bayview. Razor-sharp mountain hairpins, sheer cliff drops, and the iconic television transmission tower. Demands extreme throttle control during downhill drift competitions.',
    races: ['Mountain Drift', 'Drift King Challenge', 'Sprint'],
    shops: [
      { type: 'Car Specialties', color: '#ffcc00', name: 'Jackson Peak Specialties', desc: 'Neon underglow (pulse & multi-color), trunk audio subwoofers, scissor doors' },
      { type: 'Performance', color: '#00bfff', name: 'Summit Dyno Performance', desc: 'Stage 3 cam & head tuning, performance gear ratios, race suspension' }
    ],
    difficulty: 'Expert',
    landmarks: ['Bayview Radio Tower Summit', 'Devil\'s Hairpin Switchback', 'Reservoir Dam'],
    accentColor: '#00e5ff'
  },
  {
    id: 'coalharbour',
    name: 'Coal Harbour',
    subtitle: 'Heavy Industrial Shipyards & Steel Mills',
    stage: 'Unlocked in Stage 4 & 5',
    description: 'Grit and industry. Steel container terminals, freight train yards, narrow warehouse alleys, and high-speed elevated drawbridges over the shipping bay.',
    races: ['Drag Racing', 'Sprint', 'Circuit', 'Outrun'],
    shops: [
      { type: 'Body Shop', color: '#00ff66', name: 'Shipyard Widebody Fabricators', desc: 'Exclusive Wide Body Kits (Manteca, Vapor, Apocalypse)' },
      { type: 'Performance', color: '#00bfff', name: 'Coal Harbour Dyno Lab', desc: 'Custom Dyno ECU mapping, turbo boost calibration, nitrous purge kits' },
      { type: 'Graphics Shop', color: '#ff3366', name: 'Industrial Paint & Tint', desc: 'Chameleon pearlescent paint, window tinting, custom rims' }
    ],
    difficulty: 'Very High',
    landmarks: ['Container Terminal Cranes', 'Coal Harbour Suspension Bridge', 'Rail Freight Yards'],
    accentColor: '#ff9900'
  },
  {
    id: 'airport',
    name: 'Bayview International Airport',
    subtitle: 'Runways, Freight Hangars & Wide Straights',
    stage: 'Unlocked in Stage 4',
    description: 'Massive open tarmac runways, aircraft taxiways, and freight maintenance warehouses. The ultimate arena for maximum top-speed quarter-mile drag racing and Street X technical circuits.',
    races: ['Quarter-Mile Drag', 'Street X', 'High-Speed Circuit'],
    shops: [
      { type: 'Car Lot', color: '#ffb300', name: 'Airport Freight Import Car Lot', desc: 'Late-career top tuner cars: Nissan Skyline GT-R R34, Toyota Supra, Mazda RX-7' },
      { type: 'Car Specialties', color: '#ffcc00', name: 'Hangar Customs Specialties', desc: 'Hydraulic suspension hopping kits, custom dial gauges, nitrous purge nozzles' }
    ],
    difficulty: 'High',
    landmarks: ['Main International Runway', 'Cargo Freight Terminal 4', 'Radar Control Tower'],
    accentColor: '#e0f854'
  }
];

export default function CityLifePage({ onNavigate }) {
  const [activeDistrictId, setActiveDistrictId] = useState('citycenter');
  const [activeFilter, setActiveFilter] = useState('all');

  const currentDistrict = DISTRICTS.find((d) => d.id === activeDistrictId) || DISTRICTS[0];

  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 620, fontFamily: 'verdana, arial, sans-serif' }}>
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="/assets/images.ea.com/eagames/official/nfs/underground2/us/universal/hdr_features.jpg"
          width="615"
          alt="City Life"
          style={{ display: 'block' }}
        />
      </div>

      <div style={{ padding: '12px 18px', background: '#252e25', borderBottom: '2px solid #a1d464', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div>
          <span style={{ color: '#00ff66', fontSize: 10, fontWeight: 'bold' }}>&#9679; BAYVIEW METROPOLIS EXPLORER</span>
          <div style={{ fontFamily: 'Impact, Arial Black', fontStyle: 'italic', fontSize: 16, color: '#ffffff', letterSpacing: 1 }}>
            FIVE FREE-ROAMING NEIGHBORHOODS
          </div>
        </div>
        <div style={{ fontSize: 9, color: '#a1d464', textAlign: 'right' }}>
          <b>Free-Roam World</b><br />
          Seamless World Map
        </div>
      </div>

      <div style={{ display: 'flex', background: '#182018', borderBottom: '1px solid #2d3e2d' }}>
        {DISTRICTS.map((dist) => {
          const isActive = dist.id === activeDistrictId;
          return (
            <button
              key={dist.id}
              onClick={() => setActiveDistrictId(dist.id)}
              style={{
                flex: 1,
                padding: '8px 4px',
                background: isActive ? '#a1d464' : 'transparent',
                color: isActive ? '#000000' : '#cccccc',
                border: 'none',
                borderRight: '1px solid #283828',
                fontFamily: 'Impact, Arial Black, sans-serif',
                fontStyle: 'italic',
                fontSize: 11,
                letterSpacing: 0.8,
                cursor: 'pointer',
                transition: 'all 0.15s ease'
              }}
            >
              {dist.name.toUpperCase()}
            </button>
          );
        })}
      </div>

      <div style={{ padding: '16px 20px' }}>
        <div
          style={{
            background: 'linear-gradient(135deg, #1b261b 0%, #0d160d 70%, #060a06 100%)',
            border: '2px solid #3d593d',
            borderRadius: 3,
            padding: 16,
            color: '#ffffff',
            boxShadow: 'inset 0 0 15px rgba(0,0,0,0.8)'
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
            <div>
              <span style={{ fontSize: 9, color: currentDistrict.accentColor, fontWeight: 'bold', textTransform: 'uppercase', letterSpacing: 1 }}>
                {currentDistrict.stage} &bull; Difficulty: {currentDistrict.difficulty}
              </span>
              <div style={{ fontFamily: 'Impact, Arial Black', fontSize: 22, color: '#ffffff', fontStyle: 'italic', marginTop: 2 }}>
                {currentDistrict.name}
              </div>
              <div style={{ fontSize: 10, color: '#a1d464', fontStyle: 'italic', marginTop: 1 }}>
                {currentDistrict.subtitle}
              </div>
            </div>
            <div
              style={{
                background: 'rgba(0, 255, 100, 0.12)',
                border: `1px solid ${currentDistrict.accentColor}`,
                padding: '4px 8px',
                borderRadius: 2,
                fontSize: 9,
                fontWeight: 'bold',
                color: currentDistrict.accentColor
              }}
            >
              DISTRICT RADAR
            </div>
          </div>

          <p style={{ fontSize: 10, lineHeight: 1.6, color: '#d0dfd0', marginTop: 12, marginBottom: 14 }}>
            {currentDistrict.description}
          </p>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, borderTop: '1px solid #233523', paddingTop: 12 }}>
            <div>
              <div style={{ fontSize: 9, color: '#00ff66', fontWeight: 'bold', marginBottom: 6 }}>
                AVAILABLE RACE DISCIPLINES:
              </div>
              <ul style={{ margin: 0, paddingLeft: 16, fontSize: 10, color: '#ffffff', lineHeight: 1.6 }}>
                {currentDistrict.races.map((race, i) => (
                  <li key={i}>{race}</li>
                ))}
              </ul>

              <div style={{ fontSize: 9, color: '#00ff66', fontWeight: 'bold', marginTop: 10, marginBottom: 6 }}>
                KEY REGIONAL LANDMARKS:
              </div>
              <ul style={{ margin: 0, paddingLeft: 16, fontSize: 10, color: '#bbd0bb', lineHeight: 1.5 }}>
                {currentDistrict.landmarks.map((lm, i) => (
                  <li key={i}>{lm}</li>
                ))}
              </ul>
            </div>

            <div>
              <div style={{ fontSize: 9, color: '#a1d464', fontWeight: 'bold', marginBottom: 6 }}>
                DISCOVERABLE SHOPS IN THIS DISTRICT:
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                {currentDistrict.shops.map((shop, i) => (
                  <div
                    key={i}
                    style={{
                      background: '#121b12',
                      borderLeft: `3px solid ${shop.color}`,
                      padding: '5px 8px',
                      fontSize: 9
                    }}
                  >
                    <div style={{ display: 'flex', justifyContent: 'space-between' }}>
                      <b style={{ color: '#ffffff' }}>{shop.name}</b>
                      <span style={{ color: shop.color, fontWeight: 'bold' }}>{shop.type}</span>
                    </div>
                    <div style={{ color: '#99bb99', fontSize: 8, marginTop: 2 }}>{shop.desc}</div>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </div>

        <div style={{ marginTop: 16, background: '#e0e0e0', border: '1px solid #bbbbbb', padding: 12, borderRadius: 2 }}>
          <b style={{ fontSize: 11, color: '#222222' }}>SECRET SHOP RADAR IN NEED FOR SPEED UNDERGROUND 2:</b>
          <p style={{ fontSize: 9.5, lineHeight: 1.5, color: '#444444', margin: '6px 0 10px 0' }}>
            In Bayview, shops are hidden without upfront GPS markers. To locate hidden garages, scan the city streets for glowing colored neon streetlights:
          </p>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(5, 1fr)', gap: 6, fontSize: 8.5 }}>
            <div style={{ background: '#ffffff', border: '1px solid #00ff66', padding: 4, textAlign: 'center' }}>
              <span style={{ color: '#00aa44', fontWeight: 'bold' }}>&#9679; GREEN LIGHT</span>
              <br />Body Shop
            </div>
            <div style={{ background: '#ffffff', border: '1px solid #00bfff', padding: 4, textAlign: 'center' }}>
              <span style={{ color: '#0088cc', fontWeight: 'bold' }}>&#9679; BLUE LIGHT</span>
              <br />Performance Shop
            </div>
            <div style={{ background: '#ffffff', border: '1px solid #ffcc00', padding: 4, textAlign: 'center' }}>
              <span style={{ color: '#bb8800', fontWeight: 'bold' }}>&#9679; YELLOW LIGHT</span>
              <br />Car Specialties
            </div>
            <div style={{ background: '#ffffff', border: '1px solid #ff3366', padding: 4, textAlign: 'center' }}>
              <span style={{ color: '#cc0033', fontWeight: 'bold' }}>&#9679; RED LIGHT</span>
              <br />Graphics Shop
            </div>
            <div style={{ background: '#ffffff', border: '1px solid #ff9900', padding: 4, textAlign: 'center' }}>
              <span style={{ color: '#cc6600', fontWeight: 'bold' }}>&#9679; AMBER LIGHT</span>
              <br />Car Dealership
            </div>
          </div>
        </div>

        <div style={{ marginTop: 16, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
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
            href="#features"
            onClick={(e) => {
              e.preventDefault();
              onNavigate('features');
            }}
            style={{ color: '#336633', fontWeight: 'bold', fontSize: 10 }}
          >
            Explore Game Features &gt;&gt;
          </a>
        </div>
      </div>
    </div>
  );
}
