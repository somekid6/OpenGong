// OpenGong — example box brackets (sketch, parametric)
// The electronics box hangs from the plate's own hanger bolt(s) so it swings with the plate.
// Nothing is bent: a short length of square tube on each hanger bolt spaces the bracket off the
// steel, and the rest is straight flat bar. Two examples:
//   single — plate with ONE hanger hole: the hanger bolt clamps a tube spacer and a straight
//            drop strap. The box hangs from the strap.
//   dual   — plate with TWO hanger holes: each bolt clamps its own tube spacer, a crossbar spans
//            both, and a drop strap bolts to the crossbar's centre. Two bolts can't twist.
// Both leave the space above the bolts free for the chains, keep the box box_off off the back
// face, and stay inside the plate outline. Rubber isolates every joint (see bolt_stack()).
// These are sketches to adapt, not a drawing for one plate: measure yours and set the values.
//
// Frame: back-face view. Origin = plate centre on the back face, z = 0 is the steel, +z points
// away from the shooter, +y is up.

/* [View] */
part = "assembly";  // [assembly, side, single, dual, details]

/* [Plate] */
plate_d = 304.8;    // 12 in round
plate_t = 9.5;      // 3/8 in AR500

/* [Hanger holes] */
hanger_d = 10;      // hanger bolt diameter (M10 or 3/8 in)
single_hole_y = 127;// single hole: height of its centre above the plate centre
dual_hole_y = 118;  // two holes: height of their centres
dual_spacing = 76;  // two holes: centre-to-centre

/* [Square tube spacer] */
tube = 25;          // outside size (25 mm or 1 in)
tube_wall = 2;      // the bolt passes through the bore, so the bore must clear it

/* [Flat bar] */
bar_w = 25;         // drop strap width (aluminium 25 x 5-6 mm recommended; 25 x 3 mm if steel)
cross_w = 30;       // dual: crossbar width (room for two M5 bolts to the drop strap)
bar_t = 5;
pad_t = 3;          // rubber pad between the tube and the steel
strap_above = 20;   // drop strap length above the hanger bolt centre
cross_out = 15;     // dual: crossbar length outboard of each bolt centre

/* [Box] */
box = [120, 90, 60];// junction box body, width x height x depth
box_off = 40;       // box back face distance off the steel (3-5 cm)
box_y0 = 0;         // box bottom (cable glands) height above the plate centre
lug = [20, 12, 3];  // mounting lug top and bottom centre: width, height, thickness
lug_hole = 5;       // M4 screw clearance
strap_tail = 10;    // drop strap length below the bottom lug

/* [Preview only] */
show_pods = true;
ring_r = 120;

$fn = 48;

// ---------- derived ----------
lug_y_top = box_y0 + box[1] + lug[1]/2;     // lug hole centres
lug_y_bot = box_y0 - lug[1]/2;
strap_bot = box_y0 - lug[1] - strap_tail;
tube_s = box_off - pad_t - bar_t;           // single: strap sits straight on the tube
tube_d = box_off - pad_t - 2*bar_t;         // dual: crossbar on the tube, drop strap behind it
cross_z = pad_t + tube_d;
washer_d = hanger_d*2.6;
nut_stack = 3 + 2 + 0.8*hanger_d + 3;       // rubber washer, fender washer, nyloc, 3 threads spare

// ---------- parts ----------
module tube_spacer(len) {
  difference() {
    translate([-tube/2, -tube/2, 0]) cube([tube, tube, len]);
    translate([-tube/2 + tube_wall, -tube/2 + tube_wall, -1]) cube([tube - 2*tube_wall, tube - 2*tube_wall, len + 2]);
  }
}

// straight strap: hanger hole at the top (single) or two M5 holes (dual), box lug holes below
module strap(top, hanger_y, m5_y) {
  difference() {
    translate([-bar_w/2, strap_bot, 0]) cube([bar_w, top - strap_bot, bar_t]);
    if (hanger_y != undef) translate([0, hanger_y, -1]) cylinder(d=hanger_d + 6, h=bar_t + 2);  // for the rubber bushing
    if (m5_y != undef) for (d=[-8,8]) translate([0, m5_y + d, -1]) cylinder(d=5.5, h=bar_t + 2);
    for (y=[lug_y_top, lug_y_bot]) translate([0, y, -1]) cylinder(d=lug_hole, h=bar_t + 2);
  }
}
module single_strap() strap(single_hole_y + strap_above, single_hole_y, undef);
module drop_strap()   strap(dual_hole_y + cross_w/2, undef, dual_hole_y);

module crossbar() {
  h = dual_spacing/2;
  difference() {
    translate([-h - cross_out, dual_hole_y - cross_w/2, 0]) cube([2*(h + cross_out), cross_w, bar_t]);
    for (s=[-1,1]) translate([s*h, dual_hole_y, -1]) cylinder(d=hanger_d + 6, h=bar_t + 2);
    for (d=[-8,8]) translate([0, dual_hole_y + d, -1]) cylinder(d=5.5, h=bar_t + 2);
  }
}

// ---------- hardware ----------
module nut(d, h) { cylinder(d=d*1.7, h=h, $fn=6); }
module washer(od, t) { difference() { cylinder(d=od, h=t); translate([0,0,-1]) cylinder(d=hanger_d + 0.5, h=t + 2); } }

// hanger bolt stack, back face outward: rubber pad | square tube | flat bar (rubber bushing in
// its hole) | rubber washer | steel fender washer | nyloc nut. The chain stays on the front.
module bolt_stack(tube_len) {
  bar_z = pad_t + tube_len;
  color("#222") translate([-tube/2 - 2, -tube/2 - 2, 0]) cube([tube + 4, tube + 4, pad_t]);   // rubber pad
  color("silver") render() translate([0,0,pad_t]) tube_spacer(tube_len);
  color("dimgray") translate([0,0,-plate_t - 7]) nut(hanger_d, 7);                          // head
  color("silver") translate([0,0,-plate_t - 7]) cylinder(d=hanger_d, h=plate_t + 7 + bar_z + bar_t + nut_stack);
  color("#222") translate([0,0,bar_z]) washer(hanger_d + 6, bar_t);                          // bushing
  color("#222") translate([0,0,bar_z + bar_t]) washer(washer_d, 3);
  color("lightgray") translate([0,0,bar_z + bar_t + 3]) washer(washer_d, 2);
  color("dimgray") translate([0,0,bar_z + bar_t + 5]) difference() { nut(hanger_d, 0.8*hanger_d); translate([0,0,-1]) cylinder(d=hanger_d, h=hanger_d); }
}

module chain_up(p) {  // chain on the front of the hanger bolt, rising clear of the bracket
  color("gray") for (i=[0:3]) translate([p[0], p[1] + 8 + i*22, -plate_t - 4])
    rotate([0, i % 2 ? 90 : 0, 0]) scale([0.55, 1, 1]) rotate_extrude($fn=24) translate([11,0]) circle(d=5, $fn=12);
}

module box_proxy() {
  color("tan", 0.25) translate([-box[0]/2, box_y0, box_off + lug[2]]) cube(box);
  color("tan", 0.6) for (y=[box_y0 + box[1], box_y0 - lug[1]]) difference() {
    translate([-lug[0]/2, y, box_off]) cube([lug[0], lug[1], lug[2] + 4]);
    translate([0, y + lug[1]/2, box_off - 1]) cylinder(d=lug_hole, h=lug[2] + 6);
  }
  color("silver") for (y=[lug_y_top, lug_y_bot]) translate([0, y, box_off - bar_t - 4]) cylinder(d=4, h=bar_t + lug[2] + 8);
  color("black") for (x=[-36:18:36]) translate([x, box_y0 + 1, box_off + lug[2] + box[2]/2]) rotate([90,0,0]) cylinder(d=12, h=12);  // glands
}

// ---------- sensor pods, borrowed from the cover model for a clearance check ----------
use <../Sensor Cover/opengong_cover.scad>
sensor_ang = [for (k=[0:4]) 54 + 72*k];  // matches opengong_jig.scad and opengong_cover.scad
cov_exit = 36.4; cov_cable_z = 5.25;     // pod_end and cable centre height in the cover model at its defaults

module pods() {
  for (k=[0:4]) {
    p = [ring_r*cos(sensor_ang[k]), ring_r*sin(sensor_ang[k])];
    aim = [max(-box[0]/2 + 10, min(box[0]/2 - 10, p[0])), box_y0];  // toward the near bottom corner
    a = atan2(aim[1] - p[1], aim[0] - p[0]);
    translate(p) { sensor_proxy(); rotate([0,0,a]) {
      color(k % 2 ? "teal" : "lightseagreen") render() pod();
      for (s=[-1,1]) translate([17.6, s*20.6, 0]) magnet_proxy(); } }
    ex = [p[0] + cov_exit*cos(a), p[1] + cov_exit*sin(a), cov_cable_z];
    gl = [aim[0]*0.6, box_y0 - 6, box_off + lug[2] + box[2]/2];
    color("black") hull() { translate(ex) sphere(d=4.5, $fn=12); translate([aim[0], box_y0 - 20, 20]) sphere(d=4.5, $fn=12); }
    color("black") hull() { translate([aim[0], box_y0 - 20, 20]) sphere(d=4.5, $fn=12); translate(gl) sphere(d=4.5, $fn=12); }
  }
}

module plate_with_holes(holes) {
  color("gray", 0.35) translate([0,0,-plate_t]) difference() {
    cylinder(d=plate_d, h=plate_t, $fn=180);
    for (p=holes) translate([p[0], p[1], -1]) cylinder(d=hanger_d + 1, h=plate_t + 2);
  }
}

module single_assembly() {
  plate_with_holes([[0, single_hole_y]]);
  translate([0, single_hole_y, 0]) bolt_stack(tube_s);
  color("steelblue") render() translate([0, 0, box_off - bar_t]) single_strap();
  chain_up([0, single_hole_y]);
  box_proxy();
  if (show_pods) pods();
}

module dual_assembly() {
  h = dual_spacing/2;
  plate_with_holes([[-h, dual_hole_y], [h, dual_hole_y]]);
  for (s=[-1,1]) { translate([s*h, dual_hole_y, 0]) bolt_stack(tube_d); chain_up([s*h, dual_hole_y]); }
  color("steelblue") render() translate([0, 0, cross_z]) crossbar();
  color("cornflowerblue") render() translate([0, 0, box_off - bar_t]) drop_strap();
  color("silver") for (d=[-8,8]) translate([0, dual_hole_y + d, cross_z - 3]) cylinder(d=5, h=2*bar_t + 9);
  box_proxy();
  if (show_pods) pods();
}

module label(t, p, sz=14) { color("#333") translate(p) linear_extrude(1) text(t, size=sz, halign="center", font="Liberation Sans:style=Bold"); }

// exploded hanger bolt stack (single), laid along +z from the steel
module stack_exploded() {
  g = 12;  // gap between items
  color("gray", 0.35) translate([-25, -25, -plate_t]) cube([50, 50, plate_t]);
  color("silver") translate([0,0,-plate_t - 7]) cylinder(d=hanger_d, h=plate_t + 7 + tube_s + 6*g + 35);
  color("dimgray") translate([0,0,-plate_t - 7]) nut(hanger_d, 7);
  color("#222") translate([-tube/2 - 2, -tube/2 - 2, g]) cube([tube + 4, tube + 4, pad_t]);           // rubber pad
  color("silver") render() translate([0,0,2*g]) tube_spacer(tube_s);                                 // tube spacer
  z = 3*g + tube_s;
  color("#222") translate([0,0,z]) union() { washer(hanger_d + 6, bar_t); washer(hanger_d + 12, 1.5); } // top-hat bushing
  color("steelblue") translate([0,0,z + g]) difference() { translate([-bar_w/2, -20, 0]) cube([bar_w, 40, bar_t]);
    translate([0,0,-1]) cylinder(d=hanger_d + 6, h=bar_t + 2); }                                       // strap
  color("#222") translate([0,0,z + 2*g]) washer(washer_d, 3);
  color("lightgray") translate([0,0,z + 3*g]) washer(washer_d, 2);
  color("dimgray") translate([0,0,z + 4*g]) difference() { nut(hanger_d, 0.8*hanger_d); translate([0,0,-1]) cylinder(d=hanger_d, h=hanger_d); }
}

// bare parts laid out flat for the details view
module details() {
  translate([-200, 0, 0]) { color("steelblue") render() single_strap(); label("Single: strap", [0, strap_bot - 30, 0], 11); }
  translate([-125, 40, 0]) { color("silver") render() translate([-tube_s/2, 0, tube/2]) rotate([0, 90, 0]) tube_spacer(tube_s);
    label(str("Tube, ", tube_s, " mm"), [0, -35, 0], 11); }
  translate([-10, 0, 0]) { color("steelblue") render() crossbar(); label("Two holes: crossbar", [0, dual_hole_y - 35, 0], 11); }
  translate([-10, 40, 0]) { for (s=[-1,1]) translate([s*25 - tube_d/2, 0, tube/2]) color("silver") render() rotate([0, 90, 0]) tube_spacer(tube_d);
    label(str("2 tubes, ", tube_d, " mm"), [0, -35, 0], 11); }
  translate([100, 0, 0]) { color("cornflowerblue") render() drop_strap(); label("Drop strap", [0, strap_bot - 30, 0], 11); }
  translate([150, 60, 0]) { rotate([0, 90, 0]) stack_exploded(); label("Bolt stack", [70, -55, 0], 11); }
}

// ---------- output ----------
// Previews stand the plate up as it hangs: model +y becomes up, the back face looks toward -y.
rotate([90,0,0]) {
  if      (part == "assembly") {
    translate([-plate_d/2 - 20, 0, 0]) { single_assembly(); label("One hanger hole", [0, -plate_d/2 - 30, 0], 16); }
    translate([plate_d/2 + 20, 0, 0])  { dual_assembly();   label("Two hanger holes", [0, -plate_d/2 - 30, 0], 16); }
  }
  else if (part == "side") {  // seen from the side: the steel on the right, the shooter beyond it
    single_assembly();
    translate([0, 0, -200]) dual_assembly();
    for (l=[["One hole", 0], ["Two holes", -200]]) translate([0, -plate_d/2 - 30, l[1] + 40]) rotate([0, 90, 0]) label(l[0], [0,0,0], 14);
  }
  else if (part == "single")   single_assembly();
  else if (part == "dual")     dual_assembly();
  else if (part == "details")  details();
}

// ---------- report ----------
echo(str("SINGLE: ", tube, " mm square tube cut to ", tube_s, " mm; ", bar_w, " x ", bar_t,
         " flat bar cut to ", round(single_hole_y + strap_above - strap_bot), " mm; hanger bolt at least ",
         ceil(plate_t + pad_t + tube_s + bar_t + nut_stack), " mm under the head"));
echo(str("DUAL: 2 x ", tube, " mm square tube cut to ", tube_d, " mm; crossbar ", cross_w, " x ", bar_t,
         " cut to ", 2*(dual_spacing/2 + cross_out), " mm; drop strap ", bar_w, " x ", bar_t, " cut to ",
         round(dual_hole_y + cross_w/2 - strap_bot), " mm; hanger bolts at least ",
         ceil(plate_t + pad_t + tube_d + bar_t + nut_stack), " mm under the head"));
echo(str("BOX: ", box[0], " x ", box[1], " x ", box[2], " mm, back face ", box_off, " mm off the steel, bottom ",
         box_y0, " mm above the plate centre"));
// ---------- fit warnings ----------
if (norm([box[0]/2, box_y0 + box[1] + lug[1]]) > plate_d/2 - 5 || norm([box[0]/2, strap_bot]) > plate_d/2 - 5)
  echo("WARNING: the box or strap reaches outside the plate outline. Lower or shrink the box.");
if (box_y0 + box[1] + lug[1] > single_hole_y - washer_d/2 - 3)
  echo("WARNING: single: the box's top lug runs into the hanger nut. Lower box_y0.");
if (box_y0 + box[1] + lug[1] > dual_hole_y - 8 - 6)
  echo("WARNING: dual: the box's top lug runs into the M5 nuts behind the drop strap. Lower box_y0.");
if (tube - 2*tube_wall < hanger_d + 2)
  echo("WARNING: the tube's bore is too small for the hanger bolt.");
if (tube_d < tube/2)
  echo("WARNING: the tube spacer is shorter than half its width. Raise box_off.");
if (norm([dual_spacing/2 + cross_out, dual_hole_y + cross_w/2]) > plate_d/2 - 3)
  echo("WARNING: dual: the crossbar reaches past the plate edge. Shorten cross_out or cross_w.");
if (norm([bar_w/2, single_hole_y + strap_above]) > plate_d/2 - 3)
  echo("WARNING: single: the strap reaches past the plate edge. Shorten strap_above.");
