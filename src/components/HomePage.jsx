import React, { useState } from 'react';
import PollWidget from './PollWidget';

export default function HomePage({
  edition,
  onNavigate,
  onOpenImageModal,
  onOpenNewsletter,
  onOpenCarContest
}) {
  const isDec = edition === 'dec2004';
  const [isPlayingAudio, setIsPlayingAudio] = useState(false);

  return (
    <div style={{ width: 615 }}>
      {/* Hero 615x395 Stage */}
      <div
        style={{
          width: 615,
          height: 395,
          position: 'relative',
          background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/mainimage.jpg') no-repeat #000000`,
          backgroundSize: '615px 395px'
        }}
      >
        {/* Subtle interactive badge to view full art or toggle soundtrack vibe */}
        <div
          style={{
            position: 'absolute',
            bottom: 10,
            right: 10,
            display: 'flex',
            gap: 6
          }}
        >
          <button
            onClick={() =>
              onOpenImageModal({
                title: 'Need for Speed Underground 2 - Rachel & 350Z Hero Art',
                src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/mainimage.jpg'
              })
            }
            style={{
              background: 'rgba(0,0,0,0.65)',
              border: '1px solid #a1d464',
              color: '#a1d464',
              fontFamily: 'verdana',
              fontSize: 9,
              padding: '3px 7px',
              cursor: 'pointer'
            }}
          >
            Zoom Art
          </button>
        </div>
      </div>

      {/* 3 Columns Section (614px + 1px border) */}
      <table
        cellPadding="0"
        cellSpacing="0"
        border="0"
        width="614"
        bgcolor="#d8d8d8"
        style={{ backgroundRepeat: 'no-repeat' }}
      >
        <tbody>
          <tr>
            {/* Column 1 (152px) */}
            <td
              width="152"
              valign="top"
              bgcolor="#d8d8d8"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/bg_1.jpg') no-repeat`,
                backgroundSize: '152px auto',
                padding: '12px 10px'
              }}
            >
              {isDec ? (
                <>
                  <div className="default" style={{ marginBottom: 12 }}>
                    <b>12.06.04</b>
                    <br />
                    <b>Wallpaper Contest Update</b>
                    <div style={{ textAlign: 'center', margin: '6px 0' }}>
                      <img
                        src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/125x72_wallpaper.jpg"
                        width="125"
                        height="72"
                        alt="Wallpaper Contest"
                        style={{ border: 0, cursor: 'pointer' }}
                        onClick={() =>
                          onOpenImageModal({
                            title: 'Wallpaper Contest Finalists',
                            src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/125x72_wallpaper.jpg'
                          })
                        }
                      />
                    </div>
                    Let the voting begin! The finalists are chosen and it's time for <b>you</b> to select a winner.
                    <br />
                    <br />
                    <a
                      href="#contest"
                      onClick={(e) => {
                        e.preventDefault();
                        onOpenCarContest();
                      }}
                      style={{ color: '#000000' }}
                    >
                      Begin Voting &gt;&gt;
                    </a>
                  </div>

                  <div className="default" style={{ marginBottom: 12 }}>
                    <b>12.03.04</b>
                    <br />
                    <b>Get Tips &amp; Tricks</b>
                    <div style={{ textAlign: 'center', margin: '6px 0' }}>
                      <img
                        src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/tips_tricks_dec.jpg"
                        width="125"
                        height="175"
                        alt="Tips & Tricks"
                        style={{ border: 0, cursor: 'pointer' }}
                        onClick={() =>
                          onOpenImageModal({
                            title: 'Tips & Tricks Magazine December Issue',
                            src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/tips_tricks_dec.jpg'
                          })
                        }
                      />
                    </div>
                    Pick up the December issue of Tips &amp; Tricks Magazine to get the latest hints on{' '}
                    <i>Need for Speed Underground 2</i>.
                    <br />
                    <br />
                    <a
                      href="#tipstricks"
                      onClick={(e) => {
                        e.preventDefault();
                        alert('Tips & Tricks Magazine: December 2004 NFSU2 Strategy Guide');
                      }}
                      style={{ color: '#000000' }}
                    >
                      Learn More &gt;&gt;
                    </a>
                  </div>

                  <div className="default" style={{ marginBottom: 12 }}>
                    <b>12.03.04</b>
                    <br />
                    <b>Play <i>Underground 2</i> Online</b>
                    <br />
                    <br />
                    The Online Section for <i>Need for Speed Underground 2</i> is open for business. Check it out and see
                    what's happening <b>today</b>!
                    <br />
                    <br />
                    <a
                      href="#online"
                      onClick={(e) => {
                        e.preventDefault();
                        onNavigate('online');
                      }}
                      style={{ color: '#000000' }}
                    >
                      More Info &gt;&gt;
                    </a>
                  </div>

                  <div className="default">
                    <a
                      href="#demo"
                      onClick={(e) => {
                        e.preventDefault();
                        alert('Download the official NFSU2 PC Demo (approx 345 MB, 2004)');
                      }}
                      style={{ color: '#000000' }}
                    >
                      <b>Download the Demo</b>
                    </a>
                    <br />
                    <a
                      href="#boards"
                      onClick={(e) => {
                        e.preventDefault();
                        alert('Official EA Games Message Boards');
                      }}
                      style={{ color: '#000000' }}
                    >
                      <b>Message Boards</b>
                    </a>
                    <br />
                    <a
                      href="#newsletter"
                      onClick={(e) => {
                        e.preventDefault();
                        onOpenNewsletter();
                      }}
                      style={{ color: '#000000' }}
                    >
                      <b>Newsletter Sign Up</b>
                    </a>
                  </div>
                </>
              ) : (
                <>
                  <div className="default" style={{ marginBottom: 14 }}>
                    <b>07/06/04</b>
                    <br />
                    <b>Screenshots Update</b>
                    <br />
                    <br />
                    Four new screenshots of <i>Underground 2</i> are now available!
                    <br />
                    <br />
                    <a
                      href="#screenshots"
                      onClick={(e) => {
                        e.preventDefault();
                        onNavigate('screenshots');
                      }}
                      style={{ color: '#000000' }}
                    >
                      View the Latest Screens &gt;&gt;
                    </a>
                  </div>

                  <div className="default" style={{ marginBottom: 14 }}>
                    <b>06/30/04</b>
                    <br />
                    <b>NFSU2 Custom Car Contest</b>
                    <br />
                    <br />
                    Want to add your personal touch to <i>Underground 2</i>? Then enter the Custom Car Contest and get a
                    custom vinyl included in the game.
                    <br />
                    <br />
                    <a
                      href="#contest"
                      onClick={(e) => {
                        e.preventDefault();
                        onOpenCarContest();
                      }}
                      style={{ color: '#000000' }}
                    >
                      Find Out More &gt;&gt;
                    </a>
                  </div>

                  <div className="default">
                    <b>06/25/04</b>
                    <br />
                    <b>Links Updated</b>
                    <br />
                    New fan sites have been added to the Links section.
                    <br />
                    <br />
                    <a
                      href="#links"
                      onClick={(e) => {
                        e.preventDefault();
                        onNavigate('links');
                      }}
                      style={{ color: '#000000' }}
                    >
                      Visit the Links Section &gt;&gt;
                    </a>
                  </div>
                </>
              )}
            </td>

            {/* Divider 1 */}
            <td
              width="2"
              valign="top"
              bgcolor="#9a9a9a"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/border_1.jpg') repeat-y`,
                width: 2
              }}
            >
              <img
                src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/border_1.jpg"
                width="2"
                height="1"
                alt=""
                style={{ display: 'block' }}
              />
            </td>

            {/* Column 2 (306px) */}
            <td
              width="306"
              valign="top"
              bgcolor="#d8d8d8"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/bg_2.jpg') no-repeat`,
                backgroundSize: '306px auto',
                padding: '12px 14px'
              }}
            >
              {isDec ? (
                <>
                  <div className="default" style={{ marginBottom: 12 }}>
                    <b>Join the NFSU2 Road Crew</b>
                    <div style={{ textAlign: 'center', margin: '8px 0' }}>
                      <img
                        src="/assets/www.theroadcrew.net/tools/banners/nfsu2_road_crew_270x202.jpg"
                        width="270"
                        height="202"
                        alt="NFSU2 Road Crew"
                        style={{ border: 0, cursor: 'pointer' }}
                        onClick={() =>
                          onOpenImageModal({
                            title: 'Need for Speed Underground 2 - Road Crew',
                            src: '/assets/www.theroadcrew.net/tools/banners/nfsu2_road_crew_270x202.jpg'
                          })
                        }
                      />
                    </div>
                  </div>

                  <div className="default" style={{ marginBottom: 12 }}>
                    Join the <i>Need For Speed</i> &quot;Road Crew&quot; today and feed your need for even more speed!
                    It&apos;s a <b>FREE</b> and limited time offer so sign up today and you&apos;ll get access to slick
                    banners, buddy icons, links to cool in-game content and even your own NFS Road Crew email account!
                    <br />
                    <br />
                    Also, hit the message boards daily to talk with other NFS fans and friends. Finally, your membership
                    entitles you to plenty of chances to win some very cool prizes including a $500.00 gift card to EB
                    Games.com and all this is only available to members of the NFS Road Crew! The light is green...
                    ready to win?
                    <br />
                    <br />
                    <a
                      href="#roadcrew"
                      onClick={(e) => {
                        e.preventDefault();
                        alert('NFS Road Crew Official Community Program (Archived 2004)');
                      }}
                      style={{ color: '#000000' }}
                    >
                      Learn More &gt;&gt;
                    </a>
                  </div>

                  <div className="default">
                    <b>Need for Speed Focus Group Testing</b>
                    <br />
                    <br />
                    If you would like a chance to provide feedback on Need for Speed games and want to earn some cash for
                    the holidays, then we&apos;d love to hear from you.
                    <br />
                    <br />
                    <a
                      href="#focus"
                      onClick={(e) => {
                        e.preventDefault();
                        alert('EA Vancouver NFS Focus Group Registration');
                      }}
                      style={{ color: '#000000' }}
                    >
                      More Info &gt;&gt;
                    </a>
                  </div>
                </>
              ) : (
                <>
                  <div className="default" style={{ marginBottom: 10 }}>
                    <b>06/18/04</b>
                    <br />
                    <b>Get the <i>Road Report</i></b>
                    <br />
                    <br />
                    Like the concept art image you see below? Then{' '}
                    <a
                      href="#signup"
                      onClick={(e) => {
                        e.preventDefault();
                        onOpenNewsletter();
                      }}
                      style={{ color: '#000000', fontWeight: 'bold' }}
                    >
                      sign up
                    </a>{' '}
                    for the <i>Road Report</i> to see more exclusive Underground 2 images.
                  </div>

                  <div style={{ textAlign: 'center', margin: '8px 0' }}>
                    <img
                      src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/image_a.jpg"
                      border="1"
                      width="270"
                      height="202"
                      alt="NFSU2 Concept Art"
                      style={{ cursor: 'pointer', borderColor: '#555555' }}
                      onClick={() =>
                        onOpenImageModal({
                          title: 'Underground 2 Exclusive Concept Art',
                          src: '/assets/images.ea.com/eagames/official/nfs/underground2/us/home/image_a_big.jpg'
                        })
                      }
                      title="Click to view full size 800x600 concept art"
                    />
                    <div style={{ fontSize: 8, color: '#666666', marginTop: 3 }}>
                      (Click image to enlarge 800x600)
                    </div>
                  </div>
                </>
              )}
            </td>

            {/* Divider 2 */}
            <td
              width="2"
              valign="top"
              bgcolor="#9a9a9a"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/border_2.jpg') repeat-y`,
                width: 2
              }}
            >
              <img
                src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/border_2.jpg"
                width="2"
                height="1"
                alt=""
                style={{ display: 'block' }}
              />
            </td>

            {/* Column 3 (152px) */}
            <td
              width="152"
              valign="top"
              bgcolor="#d8d8d8"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/home/bg_3.jpg') no-repeat`,
                backgroundSize: '152px auto',
                padding: '12px 6px'
              }}
            >
              {/* Poll Widget */}
              <PollWidget edition={edition} />

              <div style={{ height: 16 }} />

              {/* Community Spotlight */}
              <div className="default" style={{ padding: '0 4px' }}>
                <b>{isDec ? '11.18.04' : '06/24/04'}</b>
                <br />
                <b>Community Spotlight</b>
                <div style={{ textAlign: 'center', margin: '8px 0' }}>
                  <img
                    src="/assets/images.ea.com/eagames/official/nfs/underground2/us/home/racerplanet.jpg"
                    width="120"
                    height="38"
                    alt="RacerPlanet.com"
                    style={{ border: 0, cursor: 'pointer' }}
                    onClick={() =>
                      alert('RacerPlanet.com - Need for Speed Fan Network (Historical Link)')
                    }
                  />
                </div>
                Racerplanet.com provides news, reviews, downloads and information on racing games as well as a large
                community forum system.
                <br />
                <br />
                Look no further to find the latest information, help and a large network of NFS fansites.
                <br />
                <br />
                <a
                  href="#spotlight"
                  onClick={(e) => {
                    e.preventDefault();
                    onNavigate('links');
                  }}
                  style={{ color: '#000000' }}
                >
                  Community Spotlight Archive
                </a>
              </div>
            </td>

            {/* Right edge border */}
            <td
              width="1"
              bgcolor="#d8d8d8"
              style={{
                background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/universal/border_2.jpg') repeat-y`,
                width: 1
              }}
            >
              <img
                src="/assets/images.ea.com/p.gif"
                width="1"
                height="225"
                alt=""
                style={{ display: 'block' }}
              />
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
}
