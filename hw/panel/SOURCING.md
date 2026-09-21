# Panel connector sourcing notes

Research date: 2026-09-21. Scope: console-side PCB-mount receptacles for the
modular front panel, plus bought SNAC adapters that can be mounted behind a
blank plate when a raw socket is unobtainable.

## Source tags and network caveat

Every claim is tagged:

- `[FETCHED url]` page actually opened in this session.
- `[SEARCH]` search-engine result title/snippet only; the page itself was not opened.
- `[MEMORY]` my own prior knowledge, no source in this session. Never treat a
  `[MEMORY]` price or part number as verified.

Domain reachability from this environment (each tried exactly once):

| Domain | Result |
|---|---|
| github.com | reachable (all repo READMEs below were fetched) |
| lcsc.com, www.lcsc.com, datasheet.lcsc.com | EGRESS_BLOCKED |
| beta.lcsc.com | DNS ENOTFOUND |
| jlcpcb.com | EGRESS_BLOCKED |
| digikey.com, mouser.com, octopart.com, jameco.com | EGRESS_BLOCKED |
| aliexpress.com, aliexpress.us | EGRESS_BLOCKED |
| lazada.com.my, shopee.com.my | EGRESS_BLOCKED |
| ebay.com, amazon.com, etsy.com, tindie.com | EGRESS_BLOCKED |
| raphnet-tech.com, console5.com, zedlabz.com, stoneagegamer.com, kasynparts.com, retro-vg.myshopify.com, retrofixes.com, nesrepairsshop.com, tinker-mods.com | EGRESS_BLOCKED |
| misteraddons.com, ultimatemister.com, retrocastlestore.com, antoniovillena.com, retroremake.co, akicus.shop, misterfpga.org, retrorgb.com, oshwlab.com | EGRESS_BLOCKED |
| adafruit.com, gct.co, pcbwiki.com, toby.co.uk, jbl-ec.com, e-switch.com, chinadaier.com, forums.nesdev.org, wiki.superfamicom.org | EGRESS_BLOCKED |

Consequence: no vendor product page was opened. All prices and part numbers
below are `[SEARCH]` snippets or `[MEMORY]` unless marked `[FETCHED]`.
Lazada and Shopee: blocked and not crawlable via search either; no listing
for any connector on either marketplace surfaced in search results. Every
Lazada/Shopee cell below is "unknown", not "unavailable".

## Summary table

Commonness: catalogue = standard distributor part; replacement = console
repair part from hobby shops / AliExpress; rare = no reliable new source found.

| # | Connector | Commonness | JLCPCB/LCSC | DigiKey/Mouser | AliExpress | Lazada | Shopee | Risk note |
|---|---|---|---|---|---|---|---|---|
| 1 | DE-9 male RA, 4-40 posts | catalogue | yes, several C-numbers [SEARCH] | yes [SEARCH] | yes (generic) [MEMORY] | unknown | unknown | many footprint variants; confirm 4-40 vs M3 and pitch |
| 2 | DA-15 female RA (2-row) | catalogue | yes [SEARCH] | yes [SEARCH] | yes (generic) [MEMORY] | unknown | unknown | do not confuse with 3-row HD15/VGA; Neo Geo plugs are deep |
| 3 | NES 7-pin socket | replacement | no | no | yes [SEARCH] | unknown | unknown | 90 deg vs 180 deg variants; pin pitch unverified |
| 4 | SNES/SFC 7-pin socket | replacement | no | no | yes [SEARCH] | unknown | unknown | 90 deg vs 180 deg variants |
| 5 | Saturn 9-pin socket | replacement / rare | no | no | one listing [SEARCH] | unknown | unknown | known sourcing problem (RetroRGB thread) |
| 6 | PlayStation socket | replacement | no | no | yes, single-port [SEARCH] | unknown | unknown | 8-pin vs 9-pin AND 90 vs 180 deg variants |
| 7 | N64 3-pin socket | replacement | no | no | yes, cheap [SEARCH] | unknown | unknown | 180 deg common; OEM ports pricey |
| 8 | GameCube socket | replacement | no | no | yes (listings churn) [SEARCH] | unknown | unknown | breakout PCB packs exist (Stone Age Gamer) |
| 9 | Dreamcast socket | rare | no | no | only repair kits / used [SEARCH] | unknown | unknown | no bare new socket found |
| 10 | Original Xbox port | rare | no | no | port PCB assemblies only [SEARCH] | unknown | unknown | no bare socket found; USB electrically |
| 11 | mini-DIN 8 female | catalogue | likely (no C-number found) | yes (CUI MD-80, Kycon KMDGX-8S) [SEARCH] | yes generic [MEMORY] | unknown | unknown | shielded vs unshielded footprints differ |
| 12 | TG-16 8-pin (full DIN-8) | catalogue-ish | unknown | unknown | unknown | unknown | unknown | US TG-16 = full-size DIN-8 270 deg, not mini-DIN [SEARCH] |
| 13 | mini-DIN 6 female | catalogue | likely (no C-number found) | yes (Kycon KMDGX-6S-BS) [SEARCH] | yes generic [SEARCH] | unknown | unknown | several body/pin-out footprints |
| 14 | mini-DIN 4 female | catalogue | yes, C6212927 [SEARCH] | yes [SEARCH] | yes generic [SEARCH] | unknown | unknown | none |
| 15 | DIN 5 180 deg female | catalogue | yes, C5780620 [SEARCH] | yes [SEARCH] | yes generic [SEARCH] | unknown | unknown | 180 deg vs 240 deg keying |
| 16 | USB-A RA single/double | catalogue | yes, single C456018 [SEARCH]; stacked unverified | yes (Molex/TE/Adam Tech stacked) [SEARCH] | yes | unknown | unknown | none |
| 17 | USB 3.0 A RA / vertical | catalogue | likely (Hong Cheng parts seen) [SEARCH] | yes (GCT USB1086, Molex 0484080003) [SEARCH] | yes | unknown | unknown | 9-pin footprint differs by maker |
| 18 | USB-C 16-pin | catalogue | yes, C165948 [SEARCH] | yes (GCT USB4085) [SEARCH] | yes | unknown | unknown | most "16-pin" parts are SMT pins + THT shell legs; full-THT rarer |
| 19 | 2x5 box header + IDC | catalogue | yes, C2977596 [SEARCH] | yes | yes | unknown | unknown | none |
| 20 | 16 mm anti-vandal ring LED | catalogue | unknown | yes (E-Switch etc.) [SEARCH] | yes (ONPOW GQ16F) [SEARCH] | unknown | unknown | LED voltage variants; some listings are 19 mm |
| 21 | 2.42" SSD1309 OLED SPI | module | no | no | yes, many [SEARCH] | unknown | unknown | 7-pin SPI vs 4-pin I2C configured by resistors |

## Per-connector detail

### 1. DE-9 male right-angle, 4-40 jack posts (Genesis / Atari, male on console)

- Catalogue part. LCSC/JLCPCB: Amphenol L717SDE09P listed as C599380 "from $0.4599" [SEARCH lcsc.com title]; TE 5747840-3 listed as C591957 "from $1.7728" [SEARCH lcsc.com title]; XUNPU D-SUB-DR-9PCM-CB C19077337 [SEARCH jlcpcb.com title]; ZHOURI D-SUB-9M-BJBJ C5156630 [SEARCH lcsc.com title]. Basic/Extended status not visible in snippets; D-subs are Extended parts on JLCPCB [MEMORY].
- Caution on L717SDE09P: one snippet described it as "solder cup termination", so verify it is the right-angle PCB variant before use [SEARCH]. TE 5747840-3 is the AMPLIMITE HD-20 right-angle plug; 4-40 threaded-insert versions exist in that family [MEMORY].
- DigiKey has L717SDE09P and the whole D-sub filter category [SEARCH]; Mouser has a "Pin (Male) Right Angle 9 Position" filter page [SEARCH].
- Generic "Renhotec D-sub 9-pin right angle male, through hole, 2.77 mm pitch, 4-40 UNC screwlocks, boardlocks" describes exactly the desired style [SEARCH metabee.com title].
- Console replacement alternative: ZedLabz "9 pin controller socket for Sega MegaDrive & Master System port internal replacement, 2-pack black" [SEARCH].
- Variant warning: pitch between pin rows (2.77 mm / 2.84 mm), footprint depth (7.2 vs 8.1 mm) and jack-post thread (4-40 vs M3) all vary by maker [MEMORY]. Fix the footprint to one datasheet.
- Breakout: DB9 breakout boards are ubiquitous on AliExpress/Amazon [MEMORY].

### 2. DA-15 female right-angle (Neo Geo / PC game port)

- Catalogue part. LCSC: CONNFLY DS1037-15FNAKT74-0CC C77835 (title says D-Sub connector assembly, 15P) [SEARCH]; the ...09FNAKT74 sibling is C75749 [SEARCH]. Amazon "Connectors Pro 10-pack DB15 female right angle PCB, 2 rows" [SEARCH]; eBay.de 2 pcs about EUR 1.19 [SEARCH].
- Winford CNR15 and NorComp DB15 pages exist for DigiKey-style parts [SEARCH].
- Warning: buy 2-row (DA-15), not 3-row HD15/VGA [SEARCH]. Neo Geo consoles use deep DA-15 sockets with a plastic shield; a standard DA-15 female accepts Neo Geo plugs once the socket is a standard part [SEARCH forums.nesdev.org snippet].
- JLCPCB assembly: Extended part if stocked [MEMORY].

### 3. NES 7-pin controller socket (console side)

- Replacement part only; not in LCSC/DigiKey.
- Sources: Console5 "Male Controller Project 7 pin Port - NES" [SEARCH]; ZedLabz "controller connector port for NES console 7 pin 90 degree, 2-pack black" [SEARCH]; Amazon BAIMOQI 90-degree 7-pin [SEARCH]; eBay 354223690636 "180 Degree 7 Pin Connector Female For NES" [SEARCH]; kasynparts "NES console 7 pin controller connector port" [SEARCH]; AliExpress wiki page about the 7-pin female replacement [SEARCH].
- Mounting: 90-degree (right angle) and 180-degree (straight) versions both sold [SEARCH]. Through-hole.
- Price: typically USD 1-3 each on AliExpress [MEMORY]; no snippet showed a price.
- Variant warning: 90 vs 180 deg; pin pitch of the aftermarket parts not documented in any snippet, so derive the footprint from a physical sample.
- Breakout: NES breakout boards sold by controlleradapter.com style shops [MEMORY]; Stone Age Gamer sells GameCube breakout packs and may have others (unverified).

### 4. SNES / Super Famicom 7-pin socket

- Replacement part. ZedLabz 90-degree grey 2-pack and 180-degree black 2-pack [SEARCH]; eBay 313735040844 "7 Pin connector Female Controller Port Replacement Console SNES" [SEARCH]; Amazon YANHAO "90 Degree Female 7 Pin ... SNES" [SEARCH]; The Retro Link "180 Degree 7 Pin Female Connector Port Socket for SNES" [SEARCH]; controlleradapter.com "SNES Controller Connector Breakout" (breakout board) [SEARCH].
- Snippet text: "through hole type for PCB mounting in a vertical orientation" [SEARCH].
- Same variant warning as NES: 90 vs 180 degree. Price USD 1-3 each [MEMORY].

### 5. Sega Saturn 9-pin socket

- Rare / replacement. RetroRGB article "Source for Saturn PCB-Mount Controller Ports?" documents this as a known sourcing problem [SEARCH]. Snippets disagree on 9 vs 10 pins.
- One AliExpress listing found: 1005004420787470 "180 Degree 9 Pin Female Slot Connector for Sega Saturn" [SEARCH]; eBay OEM port HST-3220 [SEARCH].
- The kow Saturn SNAC repo sources its Saturn connectors from an AliExpress link (no part number given) [FETCHED https://github.com/kow/MiSTer-FPGA-Sega-Saturn-SNAC-Adapter].
- Recommendation: plan for the bought-SNAC-behind-blank-plate route, or buy a bag from that AliExpress listing early and measure.

### 6. PlayStation / PS2 socket (single port)

- Replacement part, widely available. ZedLabz "8 pin 90 degree female controller connector port for PS2, 2-pack" [SEARCH]; AliExpress 1005004022772707 "2PCS full 9 pin 180 degree & 90 degree ... PS2" [SEARCH]; AliExpress cltgxdd 1005006302505323 "9-pin 90/180 degree" [SEARCH]; Amazon B0DN13YYSD "9 pin 180/90 degree female" [SEARCH]; Tindie seller "ron" "9 pin female socket for PS2" [SEARCH]; raphnet "PS1/PS2 controller connector for PCB mounting" [SEARCH]; RobotShop "PS2 connector" [SEARCH].
- Variant warning (important): listings come as 8-pin and 9-pin, and 90 vs 180 degree; a snippet explicitly warns of "mismatched pin counts" [SEARCH]. Original port is 9 positions; 8-pin parts omit the unused pin [MEMORY].
- Price: AliExpress about USD 1-2 each [MEMORY].

### 7. Nintendo 64 socket (3-pin)

- Replacement part. AliExpress 1005003720662628 "2PCS 3Pin 180 degree Connector Port for N64 ... Female Socket" at USD 0.63 [SEARCH]; raphnet N64 connector for PCB mounting [SEARCH]; RetroFixes "Original Replacement Controller Port N64" (OEM, per side) [SEARCH]; NESRepairsShop n64pt11 right-side port USD 12.99 [SEARCH]; Alibaba 3-pin listing [SEARCH].
- OEM ports are whole port assemblies; the cheap 3-pin parts are the bare socket [SEARCH].
- misteraddons/SNAC-N64 GitHub repo holds only an .rbf core file and README, no PCB files [FETCHED https://github.com/misteraddons/SNAC-N64].

### 8. GameCube socket

- Replacement part. raphnet "Gamecube controller connector" for PCB mounting [SEARCH]; Stone Age Gamer "Controller Connector w/ Breakout PCB (Pack of 4) - GameCube" [SEARCH]; AliExpress 4000393680058 "30pcs 180 degree female connector for GameCube" (listing now dead per snippet) [SEARCH]; AliExpress 3256809762579576 kit with GameCube/Dreamcast connectors [SEARCH]; ZedLabz GameCube parts collection [SEARCH].
- iFixit thread "Where can I buy a connector plug?" shows demand and thin supply [SEARCH].
- Breakout: yes (Stone Age Gamer pack of 4). Price not shown in snippets.

### 9. Dreamcast socket

- Rare. Search returned only controller-port board repair kits (fuse, cap, battery holder) at USD 5.99-8.99 [SEARCH], the 2-pin/3-pin board-to-board connector [SEARCH], and "original used 3 pin connector slot" [SEARCH]. No new bare Dreamcast controller receptacle found.
- Note there is also no Dreamcast SNAC (see SNAC section), so a Dreamcast port on the panel has no cheap path; consider omitting or using a whole donor port board.

### 10. Original Xbox controller port

- Rare as a bare socket. Tinker Mods lists an "Original Xbox controller port" replacement part (unknown whether bare socket or board) [SEARCH]; eBay 405874309242 "Original xbox 1.0 controller port pcb" [SEARCH]. Most results are Xbox-port-to-USB adapter cables [SEARCH].
- Electrically it is USB with a proprietary shell [SEARCH chris-donnelly.github.io]. A USB-A receptacle plus adapter cable is the practical substitute.

### 11. mini-DIN 8 female PCB mount (PC Engine / CD-i)

- Catalogue part. DigiKey: Same Sky/CUI MD-80SN [SEARCH]; Jameco CUI MD-80SM right-angle PC mount [SEARCH]; Kycon KMDG / KMDGX-8S right-angle series (Mouser datasheets) [SEARCH]; TE part at RS 7100450 [SEARCH]; Console5 "NEC Turbo Duo, PC Engine, SuperGrafx joystick port replacement Mini DIN 8 PCB mount", shielded, gold contacts [SEARCH].
- LCSC: no C-number surfaced in search; generic "MDC-8" style Chinese parts exist [SEARCH made-in-china]. Expect Extended if present [MEMORY].
- Price: Kycon KMDGX-4S is USD 2.28 at LCSC [SEARCH], so 8S likely similar [MEMORY].
- Variant warning: shielded (KMDGX) vs unshielded (KMDG), and mini-DIN 8 exists in more than one keying; PC Engine uses the common 8-pin layout [MEMORY].
- misteraddons PCE SNAC repo ships gerbers + JLCPCB SMT assembly files and says it uses "easily-obtainable parts" but the README does not name the mini-DIN part [FETCHED https://github.com/misteraddons/MiSTer_PCE-SNAC-KICAD].

### 12. TurboGrafx-16 8-pin socket

- US TG-16 uses a full-size DIN-8, not mini-DIN. Console5 sells "DIN 8 pin socket, PCB through hole mount, EMI shielded, C style 270" and says it replaces TG-16 controller ports, possibly needing the ground tab trimmed/relocated [SEARCH]. That is a standard DIN-8 270-degree jack, so catalogue sources (DigiKey circular DIN, LCSC "DIN-8" from Legion etc.) should cover it [MEMORY]. No LCSC C-number verified.

### 13. mini-DIN 6 female (PS/2)

- Catalogue part. DigiKey Kycon KMDGX-6S-BS [SEARCH]; eBay/Amazon uxcell shielded PS/2 PCB jacks [SEARCH]; eBay.de 50-pack MDJ104-6PSC [SEARCH]; Adafruit 804 is panel-mount, not PCB [SEARCH]. No LCSC C-number surfaced.
- Price: uxcell packs about USD 1 each [SEARCH]; Kycon about USD 2-3 [MEMORY].

### 14. mini-DIN 4 female (ADB / S-Video)

- Catalogue part. LCSC Kycon KMDGX-4S-BS C6212927 at USD 2.2836 [SEARCH]; DigiKey KMDGX-4S-BS [SEARCH]; CUI MD-40SMK / MD-40SGK [SEARCH]; uxcell 4-pack Amazon [SEARCH]. JLCPCB: Extended [MEMORY].

### 15. DIN 5 180 degree female (AT keyboard)

- Catalogue part. Kycon KCDX-5S-N: LCSC/JLCPCB C5780620 "from $0.7886", right-angle through-hole [SEARCH]; KCDX-5S-N2 C7292203 and JLCPCB C9900010111 [SEARCH]; DigiKey 10246509 / Mouser [SEARCH]. eBay "DIN 5 pin PCB mounting female sockets for PC keyboard" and "MIDI DIN5 right angle" packs [SEARCH].
- Warning: 180 deg (keyboard/MIDI) vs 240 deg keying [MEMORY].

### 16. USB-A right-angle receptacle, single and double-stacked

- Single: JLCPCB SHOU HAN "AF 90 WJDG" C456018 and "AF 90 ZJWG" C456019, USB 2.0 4-pin right angle [SEARCH]; Jing Extension "AF-90-15.5-PBT" C168713 [SEARCH]; 917-181A102ED60200 C9739 [SEARCH]. Some USB-A parts are JLCPCB Basic (C9739 historically) [MEMORY].
- Stacked: DigiKey Molex 0672984091, TE 5787745-2, Adam Tech USB-A-D-VT (8-pos, through hole, right angle) [SEARCH]. LCSC stacked C-number not found; generic "AF 90 double" parts exist [MEMORY].

### 17. USB 3.0 A: right-angle 9-pin and vertical

- LCSC Hong Cheng HC-USB3.0-C34-P C7501852 and HC-USB3.0-L115-ZP C7501867 (orientation not in snippet) [SEARCH]; Amazon uxcell "USB 3.0 Type-A female 9 pin bend foot 90 degree, 20 pcs" and "9-pin DIP 180 degree" [SEARCH]; eBay 5-pack 90-degree [SEARCH].
- Vertical: GCT USB1086 (9 contacts, through hole, vertical) [SEARCH]; Molex 0484080003 vertical through hole at DigiKey [SEARCH].
- Warning: THT 9-pin footprints differ between makers (staggered pin spacing); pin the footprint to one part.

### 18. USB-C receptacle, USB 2.0, 16-pin

- LCSC Korean Hroparts TYPE-C-31-M-12 C165948: 16 SMT signal leads plus 4 through-board shell legs, USB 2.0 subset, 5 A [SEARCH]; jenschr repo calls it the "goto part" and lists TYPE-C-31-M-14 as the newer recommendation, with LCSC USB 2.0 connectors at USD 0.24-0.38 in bulk [FETCHED https://github.com/jenschr/USB-C-Connectors]. JLCPCB generic "USB_C_Receptacle_USB2.0_16P" C9900184415 and C9900051247 [SEARCH]. DigiKey/GCT USB4085 [SEARCH].
- Warning: the 31-M-12 shell legs protrude only 0.9 mm; snippet reports boards where under-filled 1.6 mm holes let the part rip off [SEARCH]. Fully through-hole 16-pin "DIP" USB-C parts exist on AliExpress/LCSC but no verified C-number here [MEMORY].
- JLCPCB status: C165948 is Basic/Preferred in JLCPCB's library [MEMORY, historically true].

### 19. 2x5 2.54 mm shrouded box header + IDC socket

- LCSC ZHOURI DC3-2.54-10PAS C2977596 "from $0.0423", 10,450 in stock [SEARCH]; XFCN BH254V-10P C492442 [SEARCH]; Wcon 3132-10MG0BK00R2 C783843 [SEARCH]; JILN 331050SG0ABLA02 C601989 [SEARCH]; BOOMELE 2.54-2*10P C20346 USD 0.1215 [SEARCH]. IDC ribbon socket "FC-10P" everywhere (Amazon packs) [SEARCH]. Basic vs Extended not confirmed; 2x5 box headers are usually Extended [MEMORY].

### 20. 16 mm anti-vandal illuminated momentary, ring LED

- AliExpress ONPOW store: GQ16F-10E/J/RGB/S 16 mm ring LED momentary at USD 9.00 [SEARCH]; LAS1-AGQ-11E is the 19 mm sibling at USD 5.61 [SEARCH]; DAIER GQ16F-10E and LAS3D-16H-11E [SEARCH]; Rapid GQ16F-10E/J/R/12V; TME LAS1-AGQ-11E/G/12V [SEARCH]; Amazon Alpinetech 16 mm O-ring LED (2.8 V and 12 V versions) [SEARCH]; E-Switch has a 16 mm series blog [SEARCH]. DigiKey part numbers not captured. LCSC: unknown.
- Warning: LED voltage variants (3 V, 5 V, 12 V, 24 V) and 16 vs 19 mm; pin count 4 or 5.

### 21. 2.42" 128x64 SSD1309 OLED, SPI

- AliExpress 1005003091769556, 3256804452048477, 3256803920256147, 4000002579405 [SEARCH]; Amazon HiLetgo B0CFF5SD1T and ACEIRMC B0C4SKGGYF [SEARCH]; eBay 168391125148 [SEARCH]. SPI 7-pin vs I2C 4-pin selected by resistors R3/R4/R5; 3.3 V [SEARCH]. Price not shown; typically USD 8-14 [MEMORY].

## SNAC adapter products (bought-adapter fallback)

Core concept: SNAC is blue212's 3.3 V to 5 V level shifter on the MiSTer USER port, USB3-connector or HDMI-connector versions, with per-console adapter PCBs [FETCHED https://github.com/blue212/SNAC].

| Vendor | Products / consoles | Price seen | Source |
|---|---|---|---|
| MiSTer Addons (US) | SNAC V1; SNAX V2.1 (NES, SNES, PlayStation, 2-player); SNAX64 V3.1 (4-player N64 + SNAX); SNAC PC Engine; Reflex Adapt (USB, adapters for NES, SNES, N64, GameCube, Genesis, Saturn, PS, PCE, Neo Geo...) | SNAC V1 $15; SNAX $35; SNAX64 $50; SNAC PCE $30; Reflex Adapt $45 (reg. $50); HDMI-style adapter cables $10 | [SEARCH] |
| Ultimate MiSTer (EU, RetroShop.pt) | SNAC-SATURN, SNAC-ARCADE DB15 (Neo Geo), other SNACs | not shown | [SEARCH] |
| akicus.shop | SNAC NeoGeo (DB15), SNAC Saturn | not shown | [SEARCH] |
| Etsy | listing 1872874051 "Mister SNAC Adapter - SEGA Saturn"; 1872858391 "Neogeo" | not shown | [SEARCH] |
| AliExpress | 1005004114443563 "SNAC controller adapters for MiSTer USB 3.0" from $5.89; 1005009921071041 GENSMS/PCE/GB/NES; 1005003883854556 | ~$6-15 | [SEARCH] |
| eBay | N64 SNAC 325977250558 and 166727707797; NeoGeo/DB15 SNAC 325234100537 | not shown | [SEARCH] |
| Retro Remake | N64 SNAC adapter bundle | not shown | [SEARCH] |
| RetroCastle | "MiSTer FPGA SNAC Adapter" product page exists | not shown | [SEARCH] |
| Antonio Villena (antoniovillena.es/.com) | Octopod (PSX ports, DB9 SNAC + USB), Decapod (SNES, PCE/TG ports, SNAC-DB9), NeoGeo adapters, MiSTer "splitter" | not shown | [SEARCH] |
| Tindie | no SNAC listing surfaced | - | [SEARCH] |
| Amazon | B0B5LW66K7 generic SNAC adapter | not shown | [SEARCH] |

Per-console availability: NES, SNES, Genesis/SMS, PCE, PlayStation, N64 and Neo Geo/DB15 all have multiple sellers [SEARCH]. Saturn: Ultimate MiSTer, akicus, Etsy [SEARCH]. GameCube: only via Reflex Adapt (USB, not SNAC) [SEARCH]. Dreamcast: no SNAC exists; the core has no native controller support, only the NAOMI-oriented MAPLE2NAOMI adapter [SEARCH]. Forum consensus: AliExpress SNACs work but may need solder touch-up [SEARCH misterfpga.org t=5476 snippet].

Open design files:
- blue212/SNAC: README has no license statement [FETCHED github.com/blue212/SNAC].
- blue212/SNAC-PSX: KiCad + gerbers + memory-card add-on + 3D-printed case, no license stated [FETCHED github.com/blue212/SNAC-PSX].
- kow/MiSTer-FPGA-Sega-Saturn-SNAC-Adapter: EasyEDA Pro project + gerbers (May 2024), credits oshwlab patrick_3134, no license stated, connectors sourced from AliExpress [FETCHED].
- misteraddons/MiSTer_PCE-SNAC-KICAD: gerbers + JLCPCB SMT files, has a LICENSE file (type not shown) [FETCHED].
- misteraddons/SNAC-N64: only .rbf + README, no hardware files [FETCHED].
- Commercial SNAX/SNAX64/Reflex Adapt: no public design files found [SEARCH].

## What could not be verified (memory-only cells)

- All Lazada and Shopee cells: no data at all; blocked and absent from search.
- JLCPCB Basic vs Extended status for every part: [MEMORY] only (no jlcpcb.com page opened).
- Prices for NES, SNES, PS, GameCube, Saturn sockets, mini-DIN 6/8, OLED, stacked USB-A: [MEMORY] ranges; only the N64 (USD 0.63), Kycon KMDGX-4S (USD 2.28), KCDX-5S-N (USD 0.79), DC3-2.54-10PAS (USD 0.04), L717SDE09P (USD 0.46), TE 5747840-3 (USD 1.77), ONPOW buttons (USD 5.61 / 9.00) and MiSTer Addons SNAC line prices came from snippets.
- TE 5747840-3 having 4-40 inserts; L717SDE09P being right-angle PCB rather than solder-cup: unverified.
- D-sub footprint variants, DIN 180 vs 240, mini-DIN 8 keying, USB 3.0 footprint differences: [MEMORY].
- TG-16 being full-size DIN-8: [SEARCH] via Console5 snippet, but pin count/keying not confirmed.
- Existence of DB9/NES breakout boards and AliExpress generic DE-9/DA-15 sockets: [MEMORY].
- USB-C C165948 Basic status: [MEMORY].
