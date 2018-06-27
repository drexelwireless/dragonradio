whiten = [True]

soft_tx_gain_clip_frac = [0.98]

crc_schemes = ['crc32']

fec_schemes = [ ('none',   'rs8')

              , ('v27',    'none')
              , ('v27p34', 'none')
              , ('v27p78', 'none')
              , ('v27',    'rs8')

              , ('v29',    'none')
              , ('v29p34', 'none')
              , ('v29p78', 'none')
              , ('v29',    'rs8')
              ]

mod_schemes = [ ('qpsk', 'auto')
              , ('qam256', 'auto')
              ]
