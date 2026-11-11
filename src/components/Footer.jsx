import React from 'react';

export default function Footer({ edition, onSelectEdition, onOpenLegal }) {
  return (
    <div style={{ width: 765, margin: '0 auto', background: '#000000', paddingTop: 10 }}>
      <div
        style={{
          width: 765,
          height: 77,
          background: `url('./assets/images.ea.com/eagames/official/nfs/underground2/us/universal/footer.jpg') no-repeat`,
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
              Not affiliated with or endorsed by Electronic Arts.
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
            </a>
          </span>
        </div>
      </div>

      <table cellPadding="0" cellSpacing="0" border="0" width="765" style={{ marginTop: 12, marginBottom: 20 }}>
        <tbody>
          <tr>
            <td align="left">
              <img
                src="./assets/images.ea.com/eagames/official/nfs/underground2/us/e_nfsug2.gif"
                border="0"
                alt="ESRB Rating Everyone - Need for Speed Underground 2"
                style={{ verticalAlign: 'middle' }}
              />
              &nbsp;
              <img
                src="./assets/images.ea.com/esrb/privacycertified.jpg"
                border="0"
                alt="ESRB Privacy Certified"
                style={{ verticalAlign: 'middle', marginLeft: 6 }}
              />
            </td>
            <td align="right">
              {onSelectEdition && (
                <div style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
                  <span style={{ color: '#777777', fontFamily: 'verdana', fontSize: 9 }}>
                    Archive Edition:
                  </span>
                  <button
                    onClick={() => onSelectEdition('july2004')}
                    style={{
                      background: edition === 'july2004' ? '#a1d464' : '#1e241e',
                      color: edition === 'july2004' ? '#000000' : '#cccccc',
                      border: '1px solid #3a4a3a',
                      fontFamily: 'verdana',
                      fontSize: 9,
                      fontWeight: edition === 'july2004' ? 'bold' : 'normal',
                      padding: '2px 8px',
                      cursor: 'pointer'
                    }}
                  >
                    July 2004 (Pre-Release)
                  </button>
                  <button
                    onClick={() => onSelectEdition('dec2004')}
                    style={{
                      background: edition === 'dec2004' ? '#a1d464' : '#1e241e',
                      color: edition === 'dec2004' ? '#000000' : '#cccccc',
                      border: '1px solid #3a4a3a',
                      fontFamily: 'verdana',
                      fontSize: 9,
                      fontWeight: edition === 'dec2004' ? 'bold' : 'normal',
                      padding: '2px 8px',
                      cursor: 'pointer'
                    }}
                  >
                    Dec 2004 (Launch)
                  </button>
                </div>
              )}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  );
}
