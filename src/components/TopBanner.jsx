import React, { useState, useEffect } from 'react';

export default function TopBanner({ onNavigate }) {
  const [frameIndex, setFrameIndex] = useState(0);

  // Flash banner cycle frames (extracted from topbanner.swf)
  const flashFrames = [
    './assets/images.ea.com/eagames/official/nfs/underground2/us/home/topbanner.jpg'
  ];

  return (
    <div
      style={{
        width: 765,
        height: 76,
        margin: '0 auto',
        position: 'relative',
        background: `url('./assets/images.ea.com/eagames/official/nfs/underground2/us/home/topbanner.jpg') no-repeat`,
        backgroundSize: '765px 76px',
        cursor: 'pointer'
      }}
      onClick={() => onNavigate('home')}
      title="EA GAMES | Need for Speed Underground 2 - Return to Home"
    />
  );
}
