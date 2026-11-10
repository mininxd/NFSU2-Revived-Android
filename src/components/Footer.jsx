import React from 'react';

export default function Footer({ edition, onOpenLegal }) {
  const isDec = edition === 'dec2004';

  return (
    <div style={{ width: 765, margin: '0 auto', background: '#000000', paddingTop: 10 }}>
      {/* 765x77 Background Footer Strip */}
      <div
        style={{
          width: 765,
          height: 77,
          background: `url('/assets/images.ea.com/eagames/official/nfs/underground2/us/universal/footer.jpg') no-repeat`,
          position: 'relative'
        }}
      >
        <div
          style={{
            position: 'absolute',
            bottom: 6,
            left: 0,
            width: 765,
            textAlign: 'center'
          }}
        >
          <span className="legal">
            <a
              href="#legal"
              onClick={(e) => {
                e.preventDefault();
                onOpenLegal('notice');
              }}
              className="legal"
            >
              &copy;2004 Electronic Arts Inc. All Rights Reserved
            </a>
            &nbsp;|&nbsp;
            <a
              href="#privacy"
              onClick={(e) => {
                e.preventDefault();
                onOpenLegal('privacy');
              }}
              className="legal"
            >
              Privacy Policy
            </a>{' '}
            |{' '}
            <a
              href="#tos"
              onClick={(e) => {
                e.preventDefault();
                onOpenLegal('tos');
              }}
              className="legal"
            >
              Terms of Service
            </a>{' '}
            |{' '}
            <a href="mailto:eagamesonline@ea.com?subject=Need for Speed Underground 2" className="legal">
              Feedback
            </a>{' '}
            |{' '}
            <a
              href="#piracy"
              onClick={(e) => {
                e.preventDefault();
                onOpenLegal('piracy');
              }}
              className="legal"
            >
              Piracy
            </a>
          </span>
        </div>
      </div>

      {/* Badges bar */}
      <table cellPadding="0" cellSpacing="0" border="0" width="765" style={{ marginTop: 12, marginBottom: 20 }}>
        <tbody>
          <tr>
            <td align="left">
              <a href="https://www.esrb.org" target="_blank" rel="noreferrer">
                <img
                  src={
                    isDec
                      ? '/assets/images.ea.com/eagames/official/nfs/underground2/us/e_nfsug2.gif'
                      : '/assets/images.ea.com/eagames/official/defjam/fightforny/us/universal/RP_AD.gif'
                  }
                  border="0"
                  alt={isDec ? 'ESRB Rating Everyone' : 'Rating Pending'}
                  style={{ verticalAlign: 'middle' }}
                />
              </a>
              &nbsp;
              <a href="https://www.esrb.org" target="_blank" rel="noreferrer">
                <img
                  src="/assets/images.ea.com/esrb/privacycertified.jpg"
                  border="0"
                  alt="ESRB Privacy Certified"
                  style={{ verticalAlign: 'middle', marginLeft: 6 }}
                />
              </a>
            </td>
            <td align="right">
              <img
                src="/assets/images.ea.com/eagames/official/goldeneye/rogueagent/us/common/xboxlive.jpg"
                border="0"
                alt="Xbox Live"
                style={{ verticalAlign: 'middle' }}
              />
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
}
