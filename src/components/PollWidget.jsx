import React, { useState, useEffect } from 'react';

const INITIAL_POLL_DATA = [
  { id: '64305', label: 'The Official Site', votes: 412 },
  { id: '64306', label: 'Game News Sites', votes: 285 },
  { id: '64307', label: 'Magazines', votes: 147 },
  { id: '64308', label: 'Fan Sites', votes: 362 },
  { id: '64309', label: 'Friends', votes: 198 },
  { id: '64310', label: 'Other', votes: 64 }
];

export default function PollWidget({ edition }) {
  const isDec = edition === 'dec2004';
  const cookieKey = isDec ? 'nfsu2_poll_dec' : 'nfsu2_poll_july';

  const [selectedChoice, setSelectedChoice] = useState('');
  const [hasVoted, setHasVoted] = useState(false);
  const [showResults, setShowResults] = useState(false);
  const [pollData, setPollData] = useState(INITIAL_POLL_DATA);

  useEffect(() => {
    const saved = localStorage.getItem(cookieKey);
    if (saved) {
      setHasVoted(true);
      setShowResults(true);
    } else {
      setHasVoted(false);
      setShowResults(false);
    }
  }, [cookieKey]);

  const handleSubmit = (e) => {
    e.preventDefault();
    if (!selectedChoice) {
      alert('Please select an option in order to vote.');
      return;
    }
    setPollData((prev) =>
      prev.map((item) => (item.id === selectedChoice ? { ...item, votes: item.votes + 1 } : item))
    );
    setHasVoted(true);
    setShowResults(true);
    localStorage.setItem(cookieKey, 'true');
  };

  const totalVotes = pollData.reduce((acc, curr) => acc + curr.votes, 0);

  return (
    <div style={{ width: 142, padding: '0 4px', fontSize: 10, fontFamily: 'verdana, arial, sans-serif' }}>
      <span className="default">
        <b>{isDec ? '11.30.04' : '06/29/04'}</b>
        <br />
        <b>Latest Poll</b>
        <br />
      </span>
      <div style={{ margin: '6px 0', fontSize: 10 }}>
        {isDec ? 'Did you participate in the Wallpaper Contest?' : 'Where do you get your NFSU2 game information?'}
      </div>

      {!showResults ? (
        <form onSubmit={handleSubmit}>
          {pollData.map((opt) => (
            <div key={opt.id} style={{ margin: '3px 0' }}>
              <label style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 4 }}>
                <input
                  type="radio"
                  name="pollChoice"
                  value={opt.id}
                  checked={selectedChoice === opt.id}
                  onChange={(e) => setSelectedChoice(e.target.value)}
                  style={{ margin: 0 }}
                />
                <span style={{ fontSize: 9 }}>{opt.label}</span>
              </label>
            </div>
          ))}
          <div style={{ textAlign: 'center', marginTop: 8 }}>
            <input
              type="image"
              src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/votebut.gif"
              alt="Vote"
              style={{ cursor: 'pointer' }}
            />
          </div>
        </form>
      ) : (
        <div style={{ marginTop: 6 }}>
          {pollData.map((opt) => {
            const pct = Math.round((opt.votes / totalVotes) * 100);
            return (
              <div key={opt.id} style={{ margin: '4px 0', fontSize: 9 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 1 }}>
                  <span>{opt.label}</span>
                  <b>{pct}%</b>
                </div>
                <div style={{ width: '100%', height: 6, background: '#bbbbbb', borderRadius: 1 }}>
                  <div
                    style={{
                      width: `${pct}%`,
                      height: 6,
                      background: '#a1d464',
                      borderRadius: 1
                    }}
                  />
                </div>
              </div>
            );
          })}
          <div style={{ fontSize: 8, color: '#555555', textAlign: 'center', marginTop: 4 }}>
            Total votes: {totalVotes.toLocaleString()}
          </div>
          <div style={{ textAlign: 'center', marginTop: 6 }}>
            <button
              onClick={() => {
                localStorage.removeItem(cookieKey);
                setShowResults(false);
                setHasVoted(false);
              }}
              style={{
                fontFamily: 'verdana',
                fontSize: 8,
                background: '#eeeeee',
                border: '1px solid #999999',
                cursor: 'pointer',
                padding: '1px 5px'
              }}
            >
              Vote Again
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
