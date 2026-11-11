import React, { useState } from 'react';

export default function NetworkNav({ onOpenStoreFinder, onOpenLogin, onOpenSupport }) {
  return (
    <table cellSpacing="0" cellPadding="0" border="0" width="100%" bgcolor="#333333">
      <tbody>
        <tr>
          <td align="center">
            <table cellSpacing="0" cellPadding="0" border="0" width="760" bgcolor="#333333">
              <tbody>
                <tr>
                  <td rowSpan="2" style={{ padding: '4px 0' }}>
                    <a href="https://www.ea.com" target="_blank" rel="noreferrer">
                      <img
                        src="./assets/images.ea.com/eagames/uninav_logo_gray.gif"
                        border="0"
                        alt="Electronic Arts"
                        style={{ display: 'block' }}
                      />
                    </a>
                  </td>
                  <td align="right" style={{ padding: '3px 0' }}>
                    <span className="networknav">
                      <a
                        href="#support"
                        onClick={(e) => {
                          e.preventDefault();
                          onOpenSupport();
                        }}
                      >
                        SUPPORT
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a
                        href="#login"
                        onClick={(e) => {
                          e.preventDefault();
                          onOpenLogin();
                        }}
                      >
                        LOGIN
                      </a>
                    </span>
                    &nbsp;&nbsp;&nbsp;
                  </td>
                </tr>
                <tr>
                  <td align="right" style={{ padding: '3px 0 6px 0' }}>
                    <span className="networknav">
                      <a href="https://www.ea.com" target="_blank" rel="noreferrer">
                        MAIN
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a href="https://www.easports.com" target="_blank" rel="noreferrer">
                        EA SPORTS
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a href="https://www.eagames.com" style={{ color: '#FFCC00', fontWeight: 'bold' }}>
                        EA GAMES
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a href="https://www.easportsbig.com" target="_blank" rel="noreferrer">
                        EA SPORTS BIG
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a href="https://www.pogo.com" target="_blank" rel="noreferrer">
                        POGO
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a
                        href="#findastore"
                        onClick={(e) => {
                          e.preventDefault();
                          onOpenStoreFinder();
                        }}
                      >
                        FIND A STORE
                      </a>
                      <span className="networknav-pipe">|</span>
                      <a href="https://eastore.ea.com" target="_blank" rel="noreferrer">
                        EA STORE
                      </a>
                    </span>
                    &nbsp;&nbsp;&nbsp;
                  </td>
                </tr>
              </tbody>
            </table>
          </td>
        </tr>
      </tbody>
    </table>
  );
}
