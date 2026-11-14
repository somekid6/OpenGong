// OpenGong — magnetic sensor covers (parametric)
// One cover pod per sensor shields the potted piezo disc and anchors its cable a few cm
// from the disc, with the slack loop inside. Optional cable clips hold the cable run to the
// plate on its way to the electronics box. Every part is held by rubber-coated neodymium pot
// magnets bolted into the print, so nothing is drilled or glued to the plate and every part
// lifts off for calibration.
//
// Pod frame: origin = sensor centre on the plate's back face (z = 0 is the steel), the cable
// leaves along +X, roof up. Pods export roof-down so they print without supports.
// Clips export as modeled (magnet side down).

/* [Part to export] */
part = "assembly";  // [assembly, pod, clip]
pod_label = "";     // engraved on the pod roof, e.g. "S1"

/* [Sensor] */
disc_d = 15;        // piezo disc diameter (same value as the jig)
pot_clear = 6;      // radial room around the disc for the silicone potting and leads
pot_h = 10;         // headroom above the steel for the potted disc and the lead bend

/* [Cable] */
cable_d = 4.5;      // measured outside diameter of YOUR sensor cable
cable_grip = 0.2;   // groove is this much narrower than the cable, so the cable holds itself in
slack_len = 30;     // sensor centre to clamp centre; the slack loop lives in between
chan_w = 14;        // inner width of the cable channel
clamp_len = 8;      // clamp block length along the cable
tie_w = 3.6;        // cable tie width (2.5 and 3.6 mm ties both fit)
tie_t = 1.3;        // cable tie thickness

/* [Magnets] */
mag_d = 22;         // rubber-coated pot magnet diameter, rubber included
mag_h = 6;          // magnet height, rubber included, stud excluded
mag_clear = 0.4;    // diametral clearance in the locating recess
mag_recess = 1;     // how far the magnet sits up inside its recess
bolt_d = 4.4;       // clearance hole for the M4 stud or screw
mount_t = 3;        // plastic between the magnet top and the washer
cb_d = 13;          // counterbore for washer + nut or screw head (fits a 7 mm nut socket)

/* [Shell] */
wall = 2.4;
top_t = 2.4;
rim_gap = 1.5;      // the pod rim stands this far off the steel: drains water, can't rattle

/* [Cable clip] */
bundle_w = 24;      // width of the cables the clip carries side by side (5 x 4.5 mm + slack)
clip_wall = 3;
saddle_h = 3;
head_clear = 6;     // room above mount_t for a washer + nyloc nut or screw head

/* [Preview only] */
ring_r = 120;
plate_d = 304.8;
plate_t = 9.5;
box_gland = [0, 45]; // where the cables enter the box (back-face view, ring centre = origin)
box_off = 40;        // box distance off the back face (3-5 cm per System Design 4.2)

$fn = 64;

// ---------- derived ----------
pad_r   = (mag_d + mag_clear)/2 + wall;
dome_r  = disc_d/2 + pot_clear;
chan_hw = chan_w/2;
z_top   = pot_h + top_t;
mag_y   = chan_hw + pad_r;
mag_x   = sqrt(max(0, pow(dome_r + pad_r, 2) - mag_y*mag_y));
cav_end = slack_len + clamp_len/2;  // channel interior ends here; the clamp block fills its last clamp_len
pod_end = cav_end + wall;
groove_w   = cable_d - cable_grip;
groove_z0  = rim_gap + tie_t + 0.2;  // cable underside: the tie runs below it, recessed into the block
groove_top = groove_z0 + cable_d;
slot_x = tie_w + 0.6;
slot_y = tie_t + 0.5;
tie_y  = groove_w/2 + 1.2 + slot_y/2;
sensor_ang = [for (k=[0:4]) 54 + 72*k];  // S1..S5, back-face view (matches opengong_jig.scad)

// ---------- pod ----------
module cav2d() {
  circle(r=dome_r);
  translate([0, -chan_hw]) square([cav_end, chan_w]);
}
module pad_xy(s) { translate([mag_x, s*mag_y]) children(); }

module magnet_cuts(ztop) {
  translate([0,0,-1]) cylinder(d=mag_d + mag_clear, h=mag_h + 1);
  translate([0,0,-1]) cylinder(d=bolt_d, h=ztop + 2);
  translate([0,0,mag_h + mount_t]) cylinder(d=cb_d, h=ztop);
}

module pod() {
  difference() {
    union() {
      difference() {
        union() {
          translate([0,0,rim_gap]) linear_extrude(z_top - rim_gap) offset(r=wall) cav2d();
          translate([0,0,z_top - top_t]) linear_extrude(top_t) hull() {
            offset(r=wall) cav2d();
            for (s=[-1,1]) pad_xy(s) circle(r=pad_r);
          }
          for (s=[-1,1]) translate([mag_x, s*mag_y, mag_h - mag_recess]) cylinder(r=pad_r, h=z_top - mag_h + mag_recess);
        }
        translate([0,0,-1]) linear_extrude(z_top - top_t + 1) cav2d();
      }
      // clamp block across the end of the channel
      translate([cav_end - clamp_len, -chan_hw - 0.01, rim_gap]) cube([clamp_len + 0.02, chan_w + 0.02, z_top - rim_gap]);
    }
    // cable groove: a U open toward the steel, through the clamp block and the end wall
    translate([cav_end - clamp_len - 1, 0, 0]) rotate([0,90,0]) {
      translate([-(groove_top - groove_w/2), 0, 0]) cylinder(d=groove_w, h=clamp_len + wall + 2);
      translate([-(groove_top - groove_w/2), -groove_w/2, 0]) cube([groove_top - groove_w/2 + 1, groove_w, clamp_len + wall + 2]);
    }
    // cable tie: down one slot, under the cable in the recess, up the other slot, latch on the roof
    for (s=[-1,1]) translate([slack_len - slot_x/2, s*tie_y - slot_y/2, -1]) cube([slot_x, slot_y, z_top + 2]);
    translate([slack_len - slot_x/2, -tie_y - slot_y/2, -1]) cube([slot_x, 2*tie_y + slot_y, groove_z0 + 1]);
    // magnets
    for (s=[-1,1]) translate([mag_x, s*mag_y, 0]) magnet_cuts(z_top);
    // label
    if (pod_label != "") translate([0, 0, z_top - 0.6]) linear_extrude(1)
      text(pod_label, size=7, halign="center", valign="center");
    // cable-direction arrow on the roof
    translate([cav_end - clamp_len - 9, 0, z_top - 0.6]) linear_extrude(1) polygon([[-3,-3],[-3,3],[3,0]]);
  }
}

// ---------- cable clip ----------
clip_len = 2*pad_r;
clip_wid = max(2*pad_r, bundle_w + 2*clip_wall);
clip_z0  = mag_h - mag_recess;
clip_zf  = mag_h + mount_t + head_clear;  // saddle floor
clip_zt  = clip_zf + saddle_h;
clip_tx  = cb_d/2 + 1.5 + slot_x/2;

module clip() {
  difference() {
    translate([0,0,clip_z0]) linear_extrude(clip_zt - clip_z0) offset(r=3) square([clip_len - 6, clip_wid - 6], center=true);
    translate([-clip_len, -bundle_w/2, clip_zf]) cube([2*clip_len, bundle_w, clip_zt]);
    magnet_cuts(clip_zt);
    // two tie tunnels across the clip, under the saddle
    for (sx=[-1,1]) translate([sx*clip_tx - slot_x/2, -clip_wid, mag_h + 1]) cube([slot_x, 2*clip_wid, tie_t + 0.6]);
  }
}

// ---------- assembly preview (back face, ring centre = origin) ----------
module magnet_proxy() {
  color("dimgray") cylinder(d=mag_d, h=mag_h - 0.8);
  color("black") cylinder(d=mag_d - 0.6, h=0.8);
  color("silver") translate([0,0,mag_h + mount_t]) cylinder(d=9, h=3);
}
module sensor_proxy() {
  color("goldenrod") cylinder(d=disc_d, h=0.5);
  color("white", 0.7) translate([0,0,0.5]) scale([1,1,0.45]) sphere(d=disc_d + 4);
}
function sens(k) = [ring_r*cos(sensor_ang[k]), ring_r*sin(sensor_ang[k])];
function heading(p) = atan2(box_gland[1] - p[1], box_gland[0] - p[0]);
function lerp(a, b, t) = a + (b - a)*t;

module cable(a, b) { color("black") hull() { translate(a) sphere(d=cable_d, $fn=16); translate(b) sphere(d=cable_d, $fn=16); } }

module assembly() {
  color("gray", 0.35) translate([0,0,-plate_t]) cylinder(d=plate_d, h=plate_t, $fn=180);
  color("tan", 0.2) translate([box_gland[0] - 60, box_gland[1], box_off]) cube([120, 90, 60]);
  gl = [box_gland[0], box_gland[1], box_off + 30];
  for (k=[0:4]) {
    p = sens(k); a = heading(p);
    translate(p) {
      sensor_proxy();
      rotate([0,0,a]) {
        color(k % 2 ? "teal" : "lightseagreen") render() pod();
        for (s=[-1,1]) translate([mag_x, s*mag_y, 0]) magnet_proxy();
      }
    }
    ex = [p[0] + pod_end*cos(a), p[1] + pod_end*sin(a), groove_z0 + cable_d/2];
    if (k >= 2) {  // the long runs get a clip half way
      c = lerp([p[0], p[1]], box_gland, 0.5);
      translate(c) rotate([0,0,heading(c)]) { color("orange") render() clip(); magnet_proxy(); }
      cz = [c[0], c[1], clip_zf + cable_d/2];
      cable(ex, cz); cable(cz, gl);
    } else cable(ex, gl);
  }
}

// ---------- output ----------
if      (part == "assembly") assembly();
else if (part == "pod")      translate([0,0,z_top]) rotate([180,0,0]) pod();
else if (part == "clip")     translate([0,0,-clip_z0]) clip();

// ---------- report ----------
echo(str("POD footprint: ", round(pod_end + dome_r + wall), " x ", round(2*(mag_y + pad_r)), " x ", z_top, " mm"));
echo(str("FASTENER: plastic under the washer is ", mount_t, " mm. A stud must stick out at least ",
         mount_t + 0.8 + 5, " mm above the magnet for a nyloc nut; an internal-thread magnet takes an M4 screw of ",
         mount_t + 0.8, " mm + thread depth - 0.5 mm."));

// ---------- fit warnings ----------
if (mag_h - mag_recess < rim_gap + 0.5)
  echo("WARNING: magnet too thin for rim_gap — the pod rim would touch the steel. Reduce mag_recess or rim_gap.");
if (groove_top > z_top - top_t - 0.5)
  echo("WARNING: cable_d too large for pot_h — the cable groove breaks through the roof. Raise pot_h.");
if (tie_y + slot_y/2 > chan_hw - 0.8)
  echo("WARNING: tie slots break through the channel walls. Raise chan_w.");
