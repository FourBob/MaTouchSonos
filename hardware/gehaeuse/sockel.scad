// MaTouchSonos – Sockel für den Couchtisch
//
// Der Sockel hält nur den hinteren Körper (Ø 50) des Geräts. Der Kopf mit Display und Drehring
// (Ø 79) schwebt frei darüber: nichts berührt den Ring, der Drückweg (2 mm) bleibt frei.
// Das USB-C-Kabel kommt von hinten durch einen Tunnel genau in Achsrichtung zur Buchse –
// der Stecker verhindert zugleich, dass sich der Körper im Sockel mitdreht.
//
// Maße des Geräts: Makerfabs-Zeichnung (Kopf Ø 79 × 11,3, Körper Ø 50 × 26, Drückweg 2) und
// Platinendatei (USB-C-Buchse 17,9 mm neben der Körpermitte). Alles in mm.
//
// Teile (Auswahl mit „teil“ oder per Kommandozeile: openscad -D 'teil="sockel"' …):
//   "sockel"      – der Sockel (Standfläche nach unten drucken, ohne Stützen)
//   "deckel"      – Deckel für die Gewichtstasche im Boden
//   "passtest"    – nur die Aufnahme als kurzer Ring: Passung in ~10 min prüfen
//   "ansicht"     – Sockel mit Gerät (nur zum Anschauen, nicht drucken)

teil = "ansicht"; // [sockel, deckel, passtest, ansicht]

/* [Gerät] */
kopf_d = 79;          // Ø Kopf (Display + Drehring)
kopf_h = 11.3;        // Höhe Kopf
koerper_d = 50;       // Ø Körper (laut Zeichnung ±0,3)
koerper_h = 26;       // Körper, von der Rückseite des Kopfes bis hinten
drueckweg = 2;        // Kopf bewegt sich beim Drücken so weit auf den Körper zu
usb_abstand = 17.9;   // USB-C-Buchse: Abstand zur Körpermitte

/* [Aufstellung] */
neigung = 35;         // Displayfläche gegen die Senkrechte: 0 = senkrecht, 90 = liegend
griff = 10;           // so tief steckt der Körper im Sockel (hinterer Becher bis vor die Fuge)
schwebe = 16;         // Abstand unterste Kopfkante ↔ Tisch

/* [Passung] */
spiel = 0.35;         // radial: Bohrung = koerper_d + 2·spiel
rippe = 0.5;          // Quetschrippen (radial): halten das Gerät spielfrei, 0 = ohne
rippen = 6;

/* [Sockel] */
fuss_d = 92;          // Ø Standfläche
fuss_h = 6;           // Höhe des zylindrischen Fußes
wand = 3.2;           // Wand um die Aufnahme
boden = 4;            // Freiraum hinter dem Becher (Schrauben, Stecker, Bauteile)
stecker_d = 14;       // Tunnel für den USB-C-Stecker (gerade Stecker: Gehäuse ~12 × 7)
gewicht_d = 44;       // Gewichtstasche im Boden (z. B. Unterlegscheiben M10/M12)
gewicht_h = 8;
gewicht_versatz = -10; // Tasche nach vorn (−) gerückt: Abstand zur Platine mit der WLAN-Antenne
deckel_h = 1.6;
fuesse_d = 10.5;      // Mulden für selbstklebende Silikonfüße (Ø 10)
fuesse_t = 1;
fase = 0.8;
rundung = 2.5;        // Radius der oberen Fußkante

$fn = 128;

// ---------------------------------------------------------------------------------------------
// Geometrie
//
// Gerätekoordinaten: Ursprung = Mitte der Displayfront, +Z = aus dem Display heraus,
// +Y = „oben“ auf dem Display. Das Gerät liegt also bei z = −(kopf_h + koerper_h) … 0.
// Die Kippung um die X-Achse stellt es so auf, dass die Display-Normale um „neigung“ Grad
// über die Waagerechte zeigt – zum Betrachter hin (−Y der Welt).
e = neigung;
kipp = 90 - e;
bohrung_d = koerper_d + 2 * spiel;
aufnahme_d = bohrung_d + 2 * wand;
z_hinten = -(kopf_h + koerper_h);           // Rückseite des Körpers
z_mund = z_hinten + griff;                  // Oberkante des Sockels
assert(griff <= koerper_h - drueckweg - 1, "griff zu groß: Sockel würde den Drückweg blockieren");

// Weltkoordinaten eines Gerätepunkts (y, z) – nur Höhe bzw. Tiefe, für die Aufstellung
function welt_z(y, z) = y * cos(e) + z * sin(e);
function welt_y(y, z) = y * sin(e) - z * cos(e);

// Höhe der Displaymitte: tief genug für den Boden, hoch genug für den Schwebe-Abstand
hz = max(1 - welt_z(-aufnahme_d / 2, z_hinten - boden - wand),   // Außenhaut hinten unten
         5 - welt_z(-(bohrung_d / 2 - 2), z_hinten - boden) + gewicht_h,  // Innenraum über der Gewichtstasche
         schwebe - welt_z(-kopf_d / 2, -kopf_h));                // Kopf schwebt

// Standfläche so weit nach hinten, dass Drücken auf die Displaymitte den Sockel nicht nach hinten
// kippt: Hinterkante ≥ hz / tan(neigung) hinter der Displaymitte (85 %: Gewicht hilft mit).
fuss_y = max(0, 0.85 * hz / tan(e) - fuss_d / 2);

module im_geraet() { translate([0, 0, hz]) rotate([kipp, 0, 0]) children(); }

// ---------------------------------------------------------------------------------------------
// Teile

module fuss() {
    // unten kleine Fase (gegen den „Elefantenfuß“ beim Drucken), oben gerundet – weicher Übergang
    translate([0, fuss_y, 0]) hull() {
        cylinder(d = fuss_d - 2 * fase, h = fase);
        translate([0, 0, fase]) cylinder(d = fuss_d, h = fuss_h - rundung - fase);
        translate([0, 0, fuss_h - rundung]) rotate_extrude() translate([fuss_d / 2 - rundung, 0]) circle(r = rundung);
    }
}

module aufnahme_aussen() {
    im_geraet() translate([0, 0, z_hinten - boden - wand]) cylinder(d = aufnahme_d, h = griff + boden + wand);
}

module hohlraum() {
    im_geraet() {
        // Bohrung für den Körper, oben mit Einführfase, nach oben offen
        translate([0, 0, z_hinten]) cylinder(d = bohrung_d, h = 60);
        translate([0, 0, z_mund - 1.2]) cylinder(d1 = bohrung_d, d2 = bohrung_d + 2.4, h = 1.2 + 0.01);
        // Freiraum hinter dem Becher (Schraubenköpfe, Stecker, Taster) – der Rand des Bechers liegt
        // auf dem stehenbleibenden Absatz auf
        translate([0, 0, z_hinten - boden]) cylinder(d = bohrung_d - 5, h = boden + 0.01);
        // Kabeltunnel in Achsrichtung zur Buchse („oben“ im Gerät = hinten-oben am Sockel)
        translate([0, usb_abstand, z_hinten - 120]) cylinder(d = stecker_d, h = 120 + 0.01);
    }
}

module quetschrippen() {
    if (rippe > 0) im_geraet()
        for (i = [0 : rippen - 1]) rotate([0, 0, 30 + i * 360 / rippen])
            translate([bohrung_d / 2 - rippe, -0.6, z_hinten]) cube([rippe + 0.5, 1.2, griff - 1.5]);
}

module gewichtstasche() {
    translate([0, fuss_y + gewicht_versatz, -0.01]) {
        cylinder(d = gewicht_d, h = gewicht_h + deckel_h);
        cylinder(d = gewicht_d + 4, h = deckel_h + 0.01);   // Absatz für den Deckel
    }
}

module fuesse() {
    for (a = [45 : 90 : 315]) translate([0, fuss_y, 0]) rotate([0, 0, a])
        translate([fuss_d / 2 - 9, 0, -0.01]) cylinder(d = fuesse_d, h = fuesse_t + 0.01);
}

module sockel() {
    difference() {
        union() {
            difference() {
                hull() { fuss(); aufnahme_aussen(); }
                hohlraum();
            }
            // Rippen bleiben innerhalb der Aufnahme stehen (nicht im Kabeltunnel)
            difference() { quetschrippen(); im_geraet() translate([0, usb_abstand, z_hinten - 1]) cylinder(d = stecker_d, h = griff + 2); }
        }
        gewichtstasche();
        fuesse();
        translate([-200, -200, -100]) cube([400, 400, 100]);   // alles unter dem Tisch weg
    }
}

module deckel() {
    cylinder(d = gewicht_d + 4 - 0.3, h = deckel_h);
}

module passtest() {
    // Nur die Aufnahme: Ring mit Bohrung, Rippen und Einführfase, Höhe = griff
    intersection() {
        sockel();
        im_geraet() translate([0, 0, z_mund - griff]) cylinder(d = aufnahme_d + 1, h = griff + 0.01);
    }
}

// Gerät als Attrappe (für die Ansicht)
module geraet() {
    im_geraet() {
        color("silver") translate([0, 0, -kopf_h]) cylinder(d = kopf_d, h = kopf_h - 0.5);
        color("black") translate([0, 0, -0.5]) cylinder(d = kopf_d - 6, h = 0.5);
        color("dimgray") translate([0, 0, z_hinten]) cylinder(d = koerper_d, h = koerper_h);
    }
}

if (teil == "sockel") sockel();
else if (teil == "deckel") deckel();
else if (teil == "passtest") {
    // zum Drucken flach hinlegen: Mündung nach oben
    translate([0, 0, -(z_mund - griff)]) rotate([-kipp, 0, 0]) translate([0, 0, -hz]) passtest();
}
else if (teil == "ansicht") {
    color("gainsboro") sockel();
    geraet();
    echo(str("Displaymitte ", hz, " mm über dem Tisch, Fuß ", fuss_y, " mm nach hinten versetzt"));
}
