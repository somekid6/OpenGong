// OpenGong — sensor placement jig (parametric)
// One core ring (5 segments + hub) carries the sensor pockets and punch guides.
// Swappable registration adapters locate the ring on a rectangular or round plate.
//
// Coordinate frame: origin = ring center. The model is drawn as seen from the BACK
// of the plate (the face the jig sits on), +Y = up. Front-view coordinates for the
// solver are x_front = -x_model, y_front = y_model (see ECHO output).
//
// Print: ring segments and hub as modeled (flat side down, bosses up).
// Adapters are exported already flipped (plate face up) so they print without supports.
// Joints are puzzle-knob dovetails: press together on a flat surface, a drop of CA optional.

/* [Part to export] */
part = "assembly"; // [assembly, segment, hub, rect_bottom, rect_side, round_top_flex, round_left, round_right, test_bar]
segment_index = 0;      // [0:4]  S1..S5 = segment 0..4
preview_plate = "round"; // [round, rect]

/* [Sensor discs] */
disc_d = 15;            // brass disc diameter
disc_clear = 0.15;      // radial clearance disc-to-bump
bump_h = 0.6;           // how far each locating bump stands proud of the bore
bump_d = 2;
boss_wall = 4.5;
boss_h = 10;            // pocket boss height (gives a finger/press surface)
glue_chamfer = 0.8;     // relief at the plate face so squeeze-out can't lock the jig

/* [Ring] */
ring_r = 120;           // sensor circle radius (120 fits a 12 in round plate)
ring_w = 20;
jig_t = 6;
spoke_w = 8;
hub_r = 24;
lug_len = 16;           // adapter mounting lugs, one per spoke
lug_w = 18;

/* [Calibration punch guides] */
guide_d = 8;            // bore = nose diameter of YOUR automatic center punch
guide_wall = 3;
guide_h = 12;
fit_r = 60;             // fitting points: center + one per spoke at this radius
holdout_r = 92;         // held-out check points (square bosses)
holdout_spokes = [1, 2, 4];

/* [Joints] */
fit = 0.15;             // per-side clearance for puzzle joints
knob_d = 8;
neck_w = 4.5;
knob_off = 7;

/* [Rectangular plate] */
rect_center_from_bottom = 152.4; // ring center to bottom edge
rect_center_from_side = 152.4;   // ring center to the side edge with the stop
rect_side = -1;                  // -1 = stop on LEFT edge seen from back (right edge seen from front), +1 = other side
rect_plate_w = 304.8;            // preview only
rect_plate_h = 304.8;            // preview only
bar_w = 8;
stop_t = 4;
stop_w = 12;
lip = 3;                         // how far stops/hooks hang over the plate edge

/* [Round plate] */
plate_d = 304.8;
plate_t = 9.5;                   // preview only
hook_w = 22;
hook_wall = 4;
rim_clear = 0.1;
flex_len = 26;
flex_t = 1.2;
flex_gap = 1.5;
flex_preload = 0.4;              // interference that makes the top hook push the plate onto the two rigid hooks

/* [Test bar] */
test_len = 200;

$fn = 64;

// ---------- derived ----------
pocket_d = disc_d + 2*disc_clear;
hole_d   = pocket_d + 2*bump_h;
boss_d   = hole_d + 2*boss_wall;
ring_in  = ring_r - ring_w/2;
ring_out = ring_r + ring_w/2;
lug_outer = ring_out + lug_len;
pr = plate_d/2;
spoke_ang = [for (i=[0:4]) 90 + 72*i];           // 90,162,234,306,378(=18)
function sensor_ang(k) = spoke_ang[k] - 36;      // S1..S5: 54,126,198,270,342
function seg_start(k) = spoke_ang[k] - 54;
function seg_end(k)   = spoke_ang[k] + 18;

// ---------- helpers ----------
module place(r, a) { rotate([0,0,a]) translate([r,0,0]) children(); }

module sector2d(a0, a1, r=400) {
  n = 24;
  polygon(concat([[0,0]], [for (i=[0:n]) let(a=a0+(a1-a0)*i/n) [r*cos(a), r*sin(a)]]));
}

// knob pointing +x from the joint line at x=0; f = clearance (0 for knob, fit for socket)
module puzzle2d(f) {
  translate([knob_off,0]) circle(d=knob_d + 2*f, $fn=48);
  translate([-1, -(neck_w/2 + f)]) square([knob_off + 1, neck_w + 2*f]);
}
module ring_joint(c, f, h) { rotate([0,0,c]) translate([ring_r,0,0]) rotate([0,0,90])  linear_extrude(h) puzzle2d(f); }
module hub_joint(a, f, h)  { rotate([0,0,a]) translate([hub_r,0,0])  rotate([0,0,180]) linear_extrude(h) puzzle2d(f); }
module lug_joint(a, f, h)  { rotate([0,0,a]) translate([lug_outer,0,0]) rotate([0,0,180]) linear_extrude(h) puzzle2d(f); }

// ---------- core ring ----------
module ring_body() {
  difference() {
    cylinder(r=ring_out, h=jig_t, $fn=240);
    translate([0,0,-1]) cylinder(r=ring_in, h=jig_t+2, $fn=240);
  }
  for (k=[0:4]) place(ring_r, sensor_ang(k)) cylinder(d=boss_d, h=boss_h);
  for (a=spoke_ang) {
    rotate([0,0,a]) translate([hub_r, -spoke_w/2, 0]) cube([ring_in - hub_r + 1, spoke_w, jig_t]);
    rotate([0,0,a]) translate([ring_out - 2, -lug_w/2, 0]) cube([lug_len + 2, lug_w, jig_t]);
    place(fit_r, a) cylinder(d=guide_d + 2*guide_wall, h=guide_h);
  }
  for (i=holdout_spokes) place(holdout_r, spoke_ang[i])
    translate([-(guide_d/2+guide_wall), -(guide_d/2+guide_wall), 0])
      cube([guide_d+2*guide_wall, guide_d+2*guide_wall, guide_h]);
}

module pocket_cut() {
  translate([0,0,-1]) difference() {
    cylinder(d=hole_d, h=boss_h+2);
    for (b=[90,210,330]) rotate([0,0,b]) translate([pocket_d/2 + bump_d/2, 0, -1]) cylinder(d=bump_d, h=boss_h+4, $fn=24);
  }
  translate([0,0,-0.01]) cylinder(h=glue_chamfer, d1=hole_d + 2*glue_chamfer, d2=hole_d);
}

module ring_cuts() {
  for (k=[0:4]) place(ring_r, sensor_ang(k)) pocket_cut();
  for (a=spoke_ang) {
    place(fit_r, a) translate([0,0,-1]) cylinder(d=guide_d, h=guide_h+2);
    translate([0,0,-1]) lug_joint(a, fit, jig_t+2);
  }
  for (i=holdout_spokes) place(holdout_r, spoke_ang[i]) translate([0,0,-1]) cylinder(d=guide_d, h=guide_h+2);
}

module seg_label(k) {
  p = [ring_r*cos(spoke_ang[k]-18), ring_r*sin(spoke_ang[k]-18)];
  translate([p[0], p[1], jig_t - 0.6]) linear_extrude(1)
    text(str("S", k+1), size=5, halign="center", valign="center");
  if (k == 0)  // "up" arrow on the top spoke
    rotate([0,0,spoke_ang[0]]) translate([38, 0, jig_t - 0.6]) linear_extrude(1) polygon([[-4,-3],[-4,3],[4,0]]);
}

module segment(k) {
  s = seg_start(k); e = seg_end(k);
  difference() {
    union() {
      intersection() {
        ring_body();
        translate([0,0,-1]) linear_extrude(boss_h + guide_h + 2) sector2d(s, e);
      }
      ring_joint(e, 0, jig_t);
      hub_joint(spoke_ang[k], 0, jig_t);
    }
    translate([0,0,-1]) ring_joint(s, fit, jig_t + 2);
    ring_cuts();
    seg_label(k);
  }
}

module hub() {
  difference() {
    union() {
      cylinder(r=hub_r - fit, h=jig_t);
      cylinder(d=guide_d + 2*guide_wall, h=guide_h);
    }
    translate([0,0,-1]) cylinder(d=guide_d, h=guide_h + 2);
    for (a=spoke_ang) translate([0,0,-1]) hub_joint(a, fit, jig_t + 2);
  }
}

// ---------- adapters (modeled in ring frame, plate face at z=0) ----------
module adapter_root(a, len=4) {
  lug_joint(a, 0, jig_t);
  rotate([0,0,a]) translate([lug_outer + fit, -7, 0]) cube([len, 14, jig_t]);
}

module rect_bottom() {
  yb = -rect_center_from_bottom;
  A = [spoke_ang[2], spoke_ang[3]];
  xs = [for (a=A) lug_outer*cos(a)];
  x0 = min(xs) - 14; x1 = max(xs) + 14;
  for (a=A) {
    lug_joint(a, 0, jig_t);
    hull() {
      rotate([0,0,a]) translate([lug_outer + fit, -7, 0]) cube([4, 14, jig_t]);
      translate([lug_outer*cos(a) - 7, yb, 0]) cube([14, bar_w, jig_t]);
    }
  }
  translate([x0, yb, 0]) cube([x1 - x0, bar_w, jig_t]);
  for (x=[x0, x1 - stop_w]) translate([x, yb - stop_t, -lip]) cube([stop_w, stop_t, jig_t + lip]);
}

module rect_side_part() {
  a = rect_side < 0 ? spoke_ang[1] : spoke_ang[4];
  xe = rect_side * rect_center_from_side;
  yk = lug_outer*sin(a);
  xbar  = rect_side < 0 ? xe : xe - bar_w;
  xstop = rect_side < 0 ? xe - stop_t : xe;
  lug_joint(a, 0, jig_t);
  hull() {
    rotate([0,0,a]) translate([lug_outer + fit, -7, 0]) cube([4, 14, jig_t]);
    translate([xbar, yk - 7, 0]) cube([bar_w, 14, jig_t]);
  }
  translate([xbar, yk - 25, 0]) cube([bar_w, 50, jig_t]);
  translate([xstop, yk - stop_w/2, -lip]) cube([stop_t, stop_w, jig_t + lip]);
}

module hook_rigid(a) {
  lug_joint(a, 0, jig_t);
  rotate([0,0,a]) {
    translate([lug_outer + fit, -7, 0]) cube([pr + rim_clear + hook_wall - lug_outer - fit, 14, jig_t]);
    translate([pr + rim_clear, -hook_w/2, -lip]) cube([hook_wall, hook_w, jig_t + lip]);
  }
}

module hook_flex(a) {
  half = flex_len/2;
  rb0 = pr - flex_preload;       // beam inner face (interference with rim)
  rb1 = rb0 + flex_t;
  rg1 = rb1 + flex_gap;          // outer block inner face
  ro  = rg1 + hook_wall;
  lug_joint(a, 0, jig_t);
  difference() {
    rotate([0,0,a]) union() {
      translate([lug_outer + fit, -7, 0]) cube([pr - 2 - lug_outer - fit, 14, jig_t]);          // arm
      translate([pr - 6, -(half + 4), 0]) cube([4, flex_len + 8, jig_t]);                       // crossbar (above plate)
      for (sg=[-1,1]) translate([pr - 6, sg > 0 ? half : -(half + 4), 0]) cube([6.2, 4, jig_t]); // rails to posts
      for (sg=[-1,1]) translate([pr + 0.2, sg > 0 ? half : -(half + 4), -lip]) cube([ro - pr - 0.2, 4, jig_t + lip]); // posts
      translate([rb0, -half - 0.5, -lip]) cube([flex_t, flex_len + 1, jig_t + lip]);           // flex beam
      translate([rg1, -(half + 4), -lip]) cube([hook_wall, flex_len + 8, jig_t + lip]);         // outer block
    }
    // sight line + V notch on the 'up' axis: line this up with the hanger hole / your paint mark
    rotate([0,0,a]) {
      translate([lug_outer + 2, -0.4, jig_t - 0.6]) cube([pr - 8 - lug_outer, 0.8, 1]);
      translate([ro, 0, -lip - 1]) linear_extrude(jig_t + lip + 2) polygon([[0.01,-2],[0.01,2],[-2,0]]);
    }
  }
}

module flip() { translate([0,0,jig_t]) rotate([180,0,0]) children(); }

module test_bar() {
  difference() {
    cube([test_len, 12, 4]);
    for (x=[5, test_len - 5]) translate([x, 6, -1]) cylinder(d=3, h=6);
  }
}

// ---------- assembly preview ----------
module assembly() {
  if (preview_plate == "round") {
    color("gray", 0.35) translate([0,0,-plate_t]) cylinder(d=plate_d, h=plate_t, $fn=180);
    color("orange") hook_flex(spoke_ang[0]);
    color("orange") hook_rigid(spoke_ang[2]);
    color("orange") hook_rigid(spoke_ang[3]);
  } else {
    x0 = rect_side < 0 ? -rect_center_from_side : rect_center_from_side - rect_plate_w;
    color("gray", 0.35) translate([x0, -rect_center_from_bottom, -plate_t]) cube([rect_plate_w, rect_plate_h, plate_t]);
    color("orange") rect_bottom();
    color("orange") rect_side_part();
  }
  for (k=[0:4]) color(k % 2 ? "teal" : "lightseagreen") render() segment(k);
  color("white") render() hub();
}

// ---------- output ----------
if      (part == "assembly")       assembly();
else if (part == "segment")        render() segment(segment_index);
else if (part == "hub")            hub();
else if (part == "rect_bottom")    flip() rect_bottom();
else if (part == "rect_side")      flip() rect_side_part();
else if (part == "round_top_flex") flip() hook_flex(spoke_ang[0]);
else if (part == "round_left")     flip() hook_rigid(spoke_ang[2]);
else if (part == "round_right")    flip() hook_rigid(spoke_ang[3]);
else if (part == "test_bar")       test_bar();

// ---------- coordinates for the solver (front view, mm, origin = ring center) ----------
for (k=[0:4]) {
  a = sensor_ang(k);
  echo(str("SENSOR S", k+1, ": x=", round(-ring_r*cos(a)*100)/100, " y=", round(ring_r*sin(a)*100)/100));
}
echo("PUNCH fit P0: x=0 y=0");
for (i=[0:4]) {
  a = spoke_ang[i];
  echo(str("PUNCH fit P", i+1, ": x=", round(-fit_r*cos(a)*100)/100, " y=", round(fit_r*sin(a)*100)/100));
}
for (i=holdout_spokes) {
  a = spoke_ang[i];
  echo(str("PUNCH holdout H", i, ": x=", round(-holdout_r*cos(a)*100)/100, " y=", round(holdout_r*sin(a)*100)/100));
}

// ---------- fit warnings ----------
if (pr - 6 < lug_outer + fit)
  echo("WARNING: plate_d too small for ring_r — the round hooks collide with the lugs. Reduce ring_r.");
if (rect_center_from_bottom - bar_w < ring_r + boss_d/2 + 1)
  echo("WARNING: bottom bar hits the S4 pocket boss. Reduce ring_r or move the ring up.");
if (rect_center_from_side - bar_w < lug_outer*cos(18) + (lug_w/2)*sin(18) + 1)
  echo("WARNING: side bar hits the side lug. Reduce ring_r or move the ring away from the side edge.");
