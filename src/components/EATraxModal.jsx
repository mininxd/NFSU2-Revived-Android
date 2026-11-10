import React, { useState } from 'react';

const TRACKLIST = [
  { id: 1, title: 'Riders on the Storm (Fredwreck Remix)', artist: 'Snoop Dogg feat. The Doors', genre: 'Hip-Hop', duration: '6:22', youtubeUrl: 'https://www.youtube.com/watch?v=mC6qCtwt8B4' },
  { id: 2, title: 'I Do', artist: 'Chingy', genre: 'Hip-Hop', duration: '3:58', youtubeUrl: 'https://www.youtube.com/watch?v=aG-28F8b1bY' },
  { id: 3, title: 'Lean Back', artist: 'Terror Squad', genre: 'Hip-Hop', duration: '4:07', youtubeUrl: 'https://www.youtube.com/watch?v=ajmI1P3r1w4' },
  { id: 4, title: 'LAX', artist: 'Xzibit', genre: 'Hip-Hop', duration: '3:44', youtubeUrl: 'https://www.youtube.com/watch?v=vV1p20eZ7qE' },
  { id: 5, title: 'That\'z My Name', artist: 'Sly Boogy', genre: 'Hip-Hop', duration: '3:58', youtubeUrl: 'https://www.youtube.com/watch?v=x-v5p7eC7jM' },
  { id: 6, title: 'I Need Speed', artist: 'Capone', genre: 'Hip-Hop', duration: '3:31', youtubeUrl: 'https://www.youtube.com/watch?v=kYJ5oV4a2-g' },
  { id: 7, title: 'In My Head', artist: 'Queens of the Stone Age', genre: 'Rock', duration: '4:01', youtubeUrl: 'https://www.youtube.com/watch?v=6mfcmpOM454' },
  { id: 8, title: 'Give It All', artist: 'Rise Against', genre: 'Rock', duration: '2:50', youtubeUrl: 'https://www.youtube.com/watch?v=dhldbymXHT4' },
  { id: 9, title: 'The Death & Resurrection Show', artist: 'Killing Joke', genre: 'Rock', duration: '6:56', youtubeUrl: 'https://www.youtube.com/watch?v=Z3gJv4r30zY' },
  { id: 10, title: 'Black Betty', artist: 'Spiderbait', genre: 'Rock', duration: '3:26', youtubeUrl: 'https://www.youtube.com/watch?v=nU1VfYYKMDk' },
  { id: 11, title: 'Crashing Foreign Cars', artist: 'Helmet', genre: 'Rock', duration: '2:31', youtubeUrl: 'https://www.youtube.com/watch?v=i9Yy6k_w0mY' },
  { id: 12, title: 'Scavenger', artist: 'Killradio', genre: 'Rock', duration: '2:32', youtubeUrl: 'https://www.youtube.com/watch?v=Z0vjC5gZ9L0' },
  { id: 13, title: 'Notice of Eviction', artist: 'The Bronx', genre: 'Rock', duration: '2:15', youtubeUrl: 'https://www.youtube.com/watch?v=J8n5rCqK51E' },
  { id: 14, title: 'Determined', artist: 'Mudvayne', genre: 'Metal', duration: '2:39', youtubeUrl: 'https://www.youtube.com/watch?v=v9Bv25U7c_w' },
  { id: 15, title: 'No W', artist: 'Ministry', genre: 'Metal', duration: '3:14', youtubeUrl: 'https://www.youtube.com/watch?v=Yp9T0LgJq3s' },
  { id: 16, title: 'I Am Weightless', artist: 'Septembre', genre: 'Rock', duration: '3:30', youtubeUrl: 'https://www.youtube.com/watch?v=rU4fQfQc9gM' },
  { id: 17, title: 'Skeptic', artist: 'Snapcase', genre: 'Rock', duration: '2:27', youtubeUrl: 'https://www.youtube.com/watch?v=w7v_wK5cO4Q' },
  { id: 18, title: 'Nothing But You (Cirrus Remix)', artist: 'Paul Van Dyk', genre: 'Electronic', duration: '7:01', youtubeUrl: 'https://www.youtube.com/watch?v=ReqPNrn8bU4' },
  { id: 19, title: 'Switch/Twitch', artist: 'Fluke', genre: 'Electronic', duration: '9:33', youtubeUrl: 'https://www.youtube.com/watch?v=8lC6c5U8wV4' },
  { id: 20, title: 'Rush Hour', artist: 'Christopher Lawrence', genre: 'Electronic', duration: '7:47', youtubeUrl: 'https://www.youtube.com/watch?v=Fj7n0w8cO-Q' },
  { id: 21, title: 'Rocket Ride (Soulwax Remix)', artist: 'Felix Da Housecat', genre: 'Electronic', duration: '6:38', youtubeUrl: 'https://www.youtube.com/watch?v=R9K2t5iP990' },
  { id: 22, title: 'Hard EBM', artist: 'Sin', genre: 'Electronic', duration: '4:21', youtubeUrl: 'https://www.youtube.com/watch?v=r_s9G45n75M' },
  { id: 23, title: 'Mind Killer (Jagz Kooner Remix)', artist: 'FREELAND', genre: 'Electronic', duration: '6:42', youtubeUrl: 'https://www.youtube.com/watch?v=t5J5sEwFq2I' },
  { id: 24, title: 'Back on a Mission', artist: 'Cirrus', genre: 'Breakbeat', duration: '3:38', youtubeUrl: 'https://www.youtube.com/watch?v=pW-1Qc_rC8A' },
  { id: 25, title: 'The Resonator', artist: 'UNKLE', genre: 'Electronic', duration: '6:08', youtubeUrl: 'https://www.youtube.com/watch?v=aQZ_d9jK65s' },
  { id: 26, title: 'E-Ville', artist: 'Sonic Animation', genre: 'Electronic', duration: '5:02', youtubeUrl: 'https://www.youtube.com/watch?v=2n-1t6y7Bqo' },
  { id: 27, title: 'Round About Midnight', artist: 'Gotan Project', genre: 'Electronic', duration: '4:48', youtubeUrl: 'https://www.youtube.com/watch?v=L5q5L-zU84o' }
];

export default function EATraxModal({ isOpen, onClose }) {
  const [search, setSearch] = useState('');
  const [selectedGenre, setSelectedGenre] = useState('All');

  if (!isOpen) return null;

  const filteredTracks = TRACKLIST.filter((t) => {
    const matchesGenre = selectedGenre === 'All'
      ? true
      : selectedGenre === 'Rock/Metal'
      ? t.genre === 'Rock' || t.genre === 'Metal'
      : selectedGenre === 'Electronic'
      ? t.genre === 'Electronic' || t.genre === 'Breakbeat'
      : t.genre.toLowerCase() === selectedGenre.toLowerCase();

    const matchesSearch =
      t.title.toLowerCase().includes(search.toLowerCase()) ||
      t.artist.toLowerCase().includes(search.toLowerCase());

    return matchesGenre && matchesSearch;
  });

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-window"
        style={{
          width: 620,
          background: 'linear-gradient(180deg, #1b261b 0%, #0d150d 60%, #050805 100%)',
          border: '2px solid #a1d464',
          borderRadius: 4,
          padding: 16,
          boxShadow: '0 0 25px rgba(0, 255, 100, 0.45)',
          color: '#ffffff'
        }}
        onClick={(e) => e.stopPropagation()}
      >
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid #2d442d', paddingBottom: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <span style={{ color: '#00ff66', fontSize: 13 }}>&#9654;</span>
            <span style={{ fontFamily: 'Impact, Arial Black', fontStyle: 'italic', fontSize: 18, letterSpacing: 1.5, color: '#ffffff' }}>
              EA TRAX &bull; OFFICIAL MUSIC LIST
            </span>
          </div>
          <button
            onClick={onClose}
            style={{
              background: '#222222',
              border: '1px solid #555555',
              color: '#cccccc',
              cursor: 'pointer',
              fontWeight: 'bold',
              padding: '2px 8px'
            }}
          >
            [X]
          </button>
        </div>

        <div style={{ marginTop: 12, display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 10 }}>
          <input
            type="text"
            placeholder="Search track or artist name..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            style={{
              flex: 1,
              background: '#0a100a',
              border: '1px solid #3d593d',
              color: '#ffffff',
              padding: '5px 10px',
              fontFamily: 'verdana',
              fontSize: 10,
              borderRadius: 2
            }}
          />

          <a
            href="https://www.youtube.com/results?search_query=need+for+speed+underground+2+official+soundtrack+playlist"
            target="_blank"
            rel="noreferrer"
            style={{
              background: '#ff0000',
              color: '#ffffff',
              textDecoration: 'none',
              fontFamily: 'Impact, Arial Black',
              fontStyle: 'italic',
              fontSize: 11,
              padding: '5px 10px',
              borderRadius: 2,
              letterSpacing: 0.8,
              display: 'inline-flex',
              alignItems: 'center',
              gap: 4
            }}
          >
            <span>&#9658;</span> FULL SOUNDTRACK PLAYLIST
          </a>
        </div>

        <div style={{ display: 'flex', gap: 4, marginTop: 10, marginBottom: 10 }}>
          {['All', 'Hip-Hop', 'Rock/Metal', 'Electronic'].map((genre) => (
            <button
              key={genre}
              onClick={() => setSelectedGenre(genre)}
              style={{
                background: selectedGenre === genre ? '#a1d464' : '#141e14',
                color: selectedGenre === genre ? '#000000' : '#888888',
                border: '1px solid #2d442d',
                fontFamily: 'verdana',
                fontSize: 9,
                fontWeight: selectedGenre === genre ? 'bold' : 'normal',
                padding: '3px 8px',
                cursor: 'pointer'
              }}
            >
              {genre}
            </button>
          ))}
          <span style={{ marginLeft: 'auto', fontSize: 9, color: '#888888', alignSelf: 'center' }}>
            Showing {filteredTracks.length} of {TRACKLIST.length} songs
          </span>
        </div>

        <div style={{ maxHeight: 300, overflowY: 'auto', background: '#0a100a', border: '1px solid #233523', borderRadius: 2 }}>
          {filteredTracks.map((track) => (
            <div
              key={track.id}
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '7px 10px',
                borderBottom: '1px solid #141f14'
              }}
              onMouseEnter={(e) => (e.currentTarget.style.backgroundColor = 'rgba(161, 212, 100, 0.08)')}
              onMouseLeave={(e) => (e.currentTarget.style.backgroundColor = 'transparent')}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: 10, flex: 1, minWidth: 0, paddingRight: 10 }}>
                <span style={{ color: '#00ff66', fontFamily: 'Impact, Arial Black', fontSize: 13, width: 22 }}>
                  {track.id < 10 ? `0${track.id}` : track.id}
                </span>
                <div style={{ minWidth: 0 }}>
                  <div style={{ color: '#ffffff', fontSize: 11, fontWeight: 'bold', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                    {track.title}
                  </div>
                  <div style={{ color: '#a1d464', fontSize: 9, marginTop: 1 }}>
                    {track.artist}
                  </div>
                </div>
              </div>

              <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexShrink: 0 }}>
                <span style={{ fontSize: 8.5, color: '#99bb99', background: '#121e12', border: '1px solid #253825', padding: '1px 6px', borderRadius: 2 }}>
                  {track.genre}
                </span>
                <span style={{ fontSize: 9, color: '#777777', width: 28 }}>
                  {track.duration}
                </span>
                <a
                  href={track.youtubeUrl}
                  target="_blank"
                  rel="noreferrer"
                  style={{
                    background: 'linear-gradient(90deg, #a1d464 0%, #00ff66 100%)',
                    color: '#000000',
                    textDecoration: 'none',
                    fontFamily: 'Impact, Arial Black, sans-serif',
                    fontStyle: 'italic',
                    fontSize: 11,
                    letterSpacing: 0.8,
                    padding: '4px 10px',
                    borderRadius: 2,
                    display: 'inline-flex',
                    alignItems: 'center',
                    gap: 4,
                    boxShadow: '0 0 6px rgba(0,255,100,0.4)',
                    cursor: 'pointer'
                  }}
                  title={`Watch ${track.title} official video on YouTube`}
                >
                  <span>&#9658;</span> PLAY VIDEO
                </a>
              </div>
            </div>
          ))}
        </div>

        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 10, fontSize: 8.5, color: '#667766' }}>
          <span>Each play button opens the official song / music video on YouTube in a new tab.</span>
          <span>EA TRAX &copy; Electronic Arts Inc.</span>
        </div>
      </div>
    </div>
  );
}
