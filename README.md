# Need for Speed Underground 2 — Official 2004 Website Recreation

A pixel-accurate historical preservation and recreation of the official EA Games **Need for Speed Underground 2** website (`http://www.eagames.com/official/nfs/underground2/us/home.jsp`), reconstructed from Wayback Machine captures using modern **React** and **Vite**.

## Features

- **Period-Accurate Design**: Faithfully reconstructs the iconic 2004 EA Games web design, including:
  - Top EA Network Uninav bar with Support, Login, and Network directory.
  - 765x76 Top Banner.
  - Interactive Game Finder dropdown navigation toolbar.
  - 150px Left Navigation Sidebar with authentic image rollovers (`domRollover.js` behavior).
  - 615x395 Hero Showcase area displaying Rachel's iconic green Nissan 350Z.
  - 3-Column Content Layout (Screenshots Updates, Custom Car Contest, Road Report concept art, Interactive Voting Poll, Community Spotlight).
  - Authentic 2004 footer with legal notices, ESRB badges, and Xbox Live branding.
- **Dual Edition Switching**:
  - **July 2004 (Pre-Release)**: Features the early E3 trailer era, Brooke Burke announcement, contest promotions, and concept artwork.
  - **December 2004 (Official Launch)**: Features post-launch Road Crew membership, Tips & Tricks magazine guide, and demo downloads.
- **Interactive Modals & Pages**:
  - Full-screen 800x600 image zoom modal for concept art and screenshot sets.
  - Interactive Screenshots gallery with thumbnail navigation and platform filters.
  - Key Features page, News archives, and Community links.
  - Functional voting poll with live percentage result calculations and local persistence.
  - 2004 EA Store Locator and Member Login dialogs.
- **100% Offline Asset Preservation**: All 95+ original logos, icons, banners, textures, and high-resolution screenshot sets are downloaded and bundled locally under `public/assets/`.

## Getting Started

### Prerequisites

- Node.js (v18+)
- npm or yarn

### Installation

```bash
npm install
```

### Development Server

Start the local Vite development server:

```bash
npm run dev
```

The application will be accessible at `http://localhost:5173/`.

### Production Build

To create an optimized production build:

```bash
npm run build
```

Preview the build:

```bash
npm run preview
```
