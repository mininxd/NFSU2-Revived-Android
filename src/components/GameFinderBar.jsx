import React, { useState, useRef, useEffect } from 'react';

const GAMES_DATA = {
  pc: {
    label: 'PC CD-ROM',
    icon: '/assets/images.ea.com/eagames/redesign/components/gamefinder/pc-off.gif',
    games: [
      'Battlefield 1942',
      'Battlefield 2',
      'Battlefield Vietnam',
      'Command & Conquer Generals: Zero Hour',
      'Harry Potter & the Prisoner of Azkaban',
      'James Bond 007: NightFire',
      'Medal of Honor Allied Assault',
      'Medal of Honor Pacific Assault',
      'Need for Speed Hot Pursuit 2',
      'Need for Speed Underground',
      'Need for Speed Underground 2',
      'The Sims 2',
      'SimCity 4 Deluxe'
    ]
  },
  ps2: {
    label: 'PlayStation 2',
    icon: '/assets/images.ea.com/eagames/redesign/components/gamefinder/ps2-off.gif',
    games: [
      'Burnout 3: Takedown',
      'Def Jam: Fight for NY',
      'GoldenEye: Rogue Agent',
      'James Bond 007: Everything or Nothing',
      'Madden NFL 2005',
      'Medal of Honor Frontline',
      'Medal of Honor Rising Sun',
      'Need for Speed Underground',
      'Need for Speed Underground 2',
      'The Lord of the Rings: The Third Age',
      'TimeSplitters: Future Perfect'
    ]
  },
  xbox: {
    label: 'Xbox',
    icon: '/assets/images.ea.com/eagames/redesign/components/gamefinder/xbox-off.gif',
    games: [
      'Battlefield 2: Modern Combat',
      'Burnout 3: Takedown',
      'Def Jam: Fight for NY',
      'GoldenEye: Rogue Agent',
      'Need for Speed Underground',
      'Need for Speed Underground 2',
      'The Lord of the Rings: The Return of the King'
    ]
  },
  gamecube: {
    label: 'Nintendo GameCube',
    icon: '/assets/images.ea.com/eagames/redesign/components/gamefinder/gamecube-off.gif',
    games: [
      'Def Jam: Fight for NY',
      'James Bond 007: Everything or Nothing',
      'Medal of Honor European Assault',
      'Need for Speed Underground',
      'Need for Speed Underground 2',
      'The Sims Bustin Out'
    ]
  },
  gba: {
    label: 'Game Boy Advance',
    icon: '/assets/images.ea.com/eagames/redesign/components/gamefinder/gba-off.gif',
    games: [
      'Harry Potter: Quidditch World Cup',
      'Need for Speed Underground',
      'Need for Speed Underground 2',
      'The URBZ: Sims in the City'
    ]
  }
};

export default function GameFinderBar({ onSelectGame, onSelectRegion, currentRegion, onOpenSearch }) {
  const [menuOpen, setMenuOpen] = useState(false);
  const [activePlatform, setActivePlatform] = useState('pc');
  const [searchTerm, setSearchTerm] = useState('search by game name');
  const menuRef = useRef(null);

  useEffect(() => {
    function handleClickOutside(event) {
      if (menuRef.current && !menuRef.current.contains(event.target)) {
        setMenuOpen(false);
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  const handleSearchSubmit = (e) => {
    e.preventDefault();
    if (searchTerm && searchTerm !== 'search by game name') {
      onOpenSearch(searchTerm);
    }
  };

  return (
    <div
      style={{
        width: 765,
        height: 25,
        margin: '0 auto',
        background: `url('/assets/images.ea.com/eagames/redesign/common/head/find_bar.gif') repeat-x`,
        display: 'flex',
        alignItems: 'center',
        position: 'relative',
        zIndex: 100
      }}
      ref={menuRef}
    >
      <img
        src="/assets/images.ea.com/eagames/redesign/common/head/find_corner.gif"
        width="11"
        height="25"
        alt=""
        style={{ display: 'block' }}
      />

      <div style={{ position: 'relative', width: 225, height: 21 }}>
        <img
          src="/assets/images.ea.com/eagames/redesign/components/gamefinder/gamefinder.gif"
          width="225"
          height="21"
          alt="Game Finder"
          style={{ cursor: 'pointer', display: 'block' }}
          onClick={() => setMenuOpen(!menuOpen)}
        />

        {menuOpen && (
          <div
            style={{
              position: 'absolute',
              top: 21,
              left: 0,
              display: 'flex',
              background: '#e7e7e7',
              border: '1px solid #777777',
              boxShadow: '3px 3px 10px rgba(0,0,0,0.5)',
              zIndex: 200,
              fontSize: 10,
              fontFamily: 'verdana, arial, sans-serif'
            }}
          >
            <div
              style={{
                width: 140,
                borderRight: '1px solid #cccccc',
                background: '#dedede'
              }}
            >
              <div
                style={{
                  background: '#666666',
                  color: '#ffffff',
                  fontWeight: 'bold',
                  padding: '4px 8px',
                  fontSize: 9
                }}
              >
                SELECT PLATFORM
              </div>
              {Object.entries(GAMES_DATA).map(([key, data]) => {
                const isSelected = activePlatform === key;
                return (
                  <div
                    key={key}
                    onMouseEnter={() => setActivePlatform(key)}
                    style={{
                      padding: '5px 8px',
                      background: isSelected ? '#a1d464' : 'transparent',
                      color: isSelected ? '#000000' : '#222222',
                      fontWeight: isSelected ? 'bold' : 'normal',
                      cursor: 'pointer',
                      display: 'flex',
                      alignItems: 'center',
                      gap: 6
                    }}
                  >
                    <img src={data.icon} alt="" style={{ height: 12 }} />
                    <span>{data.label}</span>
                  </div>
                );
              })}
            </div>

            <div style={{ width: 230, maxHeight: 320, overflowY: 'auto', background: '#f5f5f5' }}>
              <div
                style={{
                  background: '#666666',
                  color: '#ffffff',
                  fontWeight: 'bold',
                  padding: '4px 8px',
                  fontSize: 9
                }}
              >
                {GAMES_DATA[activePlatform].label} GAMES
              </div>
              {GAMES_DATA[activePlatform].games.map((game, i) => (
                <div
                  key={i}
                  onClick={() => {
                    onSelectGame(game);
                    setMenuOpen(false);
                  }}
                  style={{
                    padding: '4px 8px',
                    borderBottom: '1px solid #e0e0e0',
                    cursor: 'pointer',
                    color: game.includes('Underground 2') ? '#006600' : '#333333',
                    fontWeight: game.includes('Underground 2') ? 'bold' : 'normal'
                  }}
                  onMouseEnter={(e) => (e.currentTarget.style.background = '#e2f4ce')}
                  onMouseLeave={(e) => (e.currentTarget.style.background = 'transparent')}
                >
                  {game}
                </div>
              ))}
            </div>
          </div>
        )}
      </div>

      <form onSubmit={handleSearchSubmit} style={{ display: 'flex', alignItems: 'center', margin: 0, paddingLeft: 6 }}>
        <input
          type="text"
          value={searchTerm}
          onFocus={() => {
            if (searchTerm === 'search by game name') setSearchTerm('');
          }}
          onBlur={() => {
            if (!searchTerm) setSearchTerm('search by game name');
          }}
          onChange={(e) => setSearchTerm(e.target.value)}
          style={{
            fontFamily: 'verdana',
            fontSize: '7pt',
            backgroundColor: '#a1a1a1',
            color: '#000000',
            border: '1px solid #777777',
            width: 140,
            height: 16,
            padding: '1px 3px'
          }}
        />
        <input
          type="image"
          src="/assets/images.ea.com/eagames/redesign/common/buttons/search_go.jpg"
          alt="Go"
          style={{ marginLeft: 4, cursor: 'pointer', verticalAlign: 'middle' }}
        />
      </form>

      <div style={{ display: 'flex', alignItems: 'center', marginLeft: 8 }}>
        <a
          href="#register"
          onClick={(e) => {
            e.preventDefault();
            alert('Electronic Arts Member Registration (2004 archive recreation)');
          }}
        >
          <img
            src="/assets/images.ea.com/eagames/redesign/common/head/register.gif"
            border="0"
            alt="Register"
            style={{ display: 'block' }}
          />
        </a>
        <a
          href="#support"
          onClick={(e) => {
            e.preventDefault();
            alert('EA Customer Support: Need for Speed Underground 2 Knowledgebase');
          }}
          style={{ marginLeft: 3 }}
        >
          <img
            src="/assets/images.ea.com/eagames/redesign/common/head/support.gif"
            border="0"
            alt="Support"
            style={{ display: 'block' }}
          />
        </a>
      </div>

      <div style={{ marginLeft: 'auto', marginRight: 8 }}>
        <select
          value={currentRegion}
          onChange={(e) => onSelectRegion(e.target.value)}
          style={{
            fontFamily: 'arial',
            fontSize: '8pt',
            backgroundColor: '#cccccc',
            color: '#000000',
            border: '1px solid #666666',
            height: 18
          }}
        >
          <option value="us">North America</option>
          <option value="cz">Czech Republic</option>
          <option value="fr">France</option>
          <option value="de">Germany</option>
          <option value="it">Italy</option>
          <option value="jp">Japan</option>
          <option value="pl">Poland</option>
          <option value="es">Spain</option>
        </select>
      </div>
    </div>
  );
}
