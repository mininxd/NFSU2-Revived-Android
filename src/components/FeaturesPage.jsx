import React from 'react';

export default function FeaturesPage({ onNavigate }) {
  return (
    <div style={{ width: 615, background: '#d8d8d8', minHeight: 500 }}>
      <div style={{ width: 615, height: 100, overflow: 'hidden' }}>
        <img
          src="./assets/images.ea.com/eagames/official/nfs/underground2/us/universal/hdr_features.jpg"
          width="615"
          alt="Features"
          style={{ display: 'block' }}
        />
      </div>

      <div style={{ padding: '16px 20px', fontFamily: 'verdana, arial, sans-serif', fontSize: 10, lineHeight: 1.5 }}>
        <div style={{ borderBottom: '1px solid #bbbbbb', paddingBottom: 8, marginBottom: 14 }}>
          <span style={{ fontSize: 12, fontWeight: 'bold', color: '#111111' }}>
            NEED FOR SPEED UNDERGROUND 2 - KEY FEATURES
          </span>
        </div>

        <ul style={{ paddingLeft: 18, margin: 0 }}>
          <li style={{ marginBottom: 12 }}>
            <b>Free-Roaming City</b>: Explore a massive city divided into five distinct, interconnected neighborhoods,
            ranging from serpentine suburban hills to an intense downtown grid. A complex series of drivable freeways
            connect the neighborhoods together.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>Emergent Gameplay</b>: Want to make a name for yourself in the underground? You&apos;ll need to find it first.
            Races can happen anywhere and at any time and as players encounter rivals on the street they&apos;ll be clued
            into where they can buy the hottest upgrades or enter the most elite events.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>New Gameplay Modes</b>: Three new gameplay modes are added to last year&apos;s circuit, drift and drag
            competitions, challenging gamers to hone their skills in a number of driving disciplines.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>Performance Tuning</b>: Tune every aspect of your car&apos;s performance with hundreds of licensed
            aftermarket performance upgrades and then fine-tune everything from the gear ratio to the suspension for the
            ultimate ride.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>The Hottest Cars</b>: More than thirty of the most sought-after tuner cars are available from the hottest
            manufacturers—from Mitsubishi Motors, Nissan, and Toyota to Ford and Volkswagen, if they&apos;re hot on the street,
            they&apos;re in the game.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>Visual Customization</b>: With more than twice the visual customizations of last year&apos;s game, there are
            literally billions of car combinations available to gamers. Featuring audio upgrades, scissor doors, spoilers,
            headlights, side skirts, roof scoops, hoods, rims, wide body kits, neon and more, if you can trick it out in
            real life, you can trick it out in Need for Speed Underground 2.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>Environmental Variety</b>: Each of the five distinct neighborhoods in Need for Speed Underground 2 feature a
            unique look and feel, including varied driving conditions and track types. Road surfaces, real-time weather
            effects, and environmental hazards will keep gamers on their toes as they race from dusk till dawn.
          </li>
          <li style={{ marginBottom: 12 }}>
            <b>Online</b>: Gamers can put their reputation on the line, challenging the world&apos;s best racers online for the
            PlayStation&reg;2 computer entertainment system and PC.
          </li>
        </ul>

        <div style={{ marginTop: 20, fontSize: 9, color: '#666666', borderTop: '1px solid #cccccc', paddingTop: 8 }}>
          *An Internet connection, Network Adaptor and Memory Card for the PlayStation 2 are required for online game play.
        </div>

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
