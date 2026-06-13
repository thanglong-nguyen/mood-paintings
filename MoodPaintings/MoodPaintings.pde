import java.net.*;

// ================================================================
// UDP
// ================================================================
DatagramSocket socket;
DatagramPacket packet;
byte[] buffer = new byte[256];

// ================================================================
// MOOD STATE
// ================================================================
float currentBPM   = 70;
float currentRMSSD = 50;
String currentMood = "Calm";

float turbulence       = 0.0;
float targetTurbulence = 0.0;

// ================================================================
// DEBUG LOG
// ================================================================
String[] debugLog    = new String[5];
int      debugLogIndex = 0;
int      packetCount   = 0;
float    signalStrength = 0.0;

// ================================================================
// COLOUR PALETTES — 8 shades per mood
// ================================================================
int[][][] palettes = {
  // Calm — midnight navy → sky blue → pale white blue
  {{5,15,50},{10,30,90},{20,60,140},{35,95,180},
   {60,140,200},{100,175,220},{155,205,235},{210,230,245}},
  // Neutral — deep forest → sage → soft teal → mist
  {{20,55,45},{35,85,65},{55,120,95},{80,150,125},
   {105,165,150},{135,180,170},{170,200,190},{205,220,215}},
  // Stressed — deep rust → burning orange → amber
  {{90,20,0},{160,45,5},{210,80,10},{235,120,20},
   {245,155,40},{250,185,70},{253,210,110},{255,230,160}},
  // HighStress — violent crimson → blood red → burning orange
  {{60,0,0},{120,0,5},{180,10,10},{220,30,20},
   {235,60,15},{245,90,20},{255,120,30},{255,60,60}}
};

int   currentPaletteIndex = 0;
int   targetPaletteIndex  = 0;
float paletteBlend        = 0.0;

// ================================================================
// BRUSH AGENTS
// ================================================================
int NUM_AGENTS = 12;
BrushAgent[] agents;

// ================================================================
// SETUP
// ================================================================
void setup() {
  fullScreen();
  background(255, 255, 255);
  smooth();
  colorMode(RGB, 255);

  for (int i = 0; i < debugLog.length; i++) debugLog[i] = "";

  agents = new BrushAgent[NUM_AGENTS];
  for (int i = 0; i < NUM_AGENTS; i++) agents[i] = new BrushAgent();

  try {
    socket = new DatagramSocket(5005);
    socket.setSoTimeout(10);
    println("UDP listening on port 5005");
  } catch (Exception e) {
    println("UDP error: " + e.getMessage());
  }
}

// ================================================================
// DRAW
// ================================================================
void draw() {

  receiveUDP();

  signalStrength = max(0, signalStrength - 0.008);

  turbulence   += (targetTurbulence - turbulence)  * 0.005;
  paletteBlend += (1.0 - paletteBlend) * 0.002;

  for (BrushAgent a : agents) { a.update(); a.draw(); }

  drawHUD();
}

// ================================================================
// UDP RECEIVER
// ================================================================
void receiveUDP() {
  try {
    packet = new DatagramPacket(buffer, buffer.length);
    socket.receive(packet);
    String line = new String(packet.getData(), 0, packet.getLength()).trim();
    println("UDP: " + line);

    debugLog[debugLogIndex % 5] = line;
    debugLogIndex++;
    packetCount++;
    signalStrength = 1.0;

    String[] parts = split(line, ',');
    for (String part : parts) {
      String[] kv = split(part, ':');
      if (kv.length < 2) continue;
      if (kv[0].equals("BPM"))   currentBPM   = float(kv[1]);
      if (kv[0].equals("RMSSD")) currentRMSSD = float(kv[1]);
      if (kv[0].equals("MOOD"))  setMood(kv[1]);
    }
  } catch (Exception e) { /* timeout */ }
}

// ================================================================
// BRUSH AGENT
// ================================================================
class BrushAgent {
  float x, y, angle, noiseOffset, radius, speed;
  int   colorIndex;

  BrushAgent() { reset(); }

  void reset() {
    x = random(width); y = random(height);
    angle = random(TWO_PI);
    noiseOffset = random(1000);
    colorIndex  = int(random(8));
    updateSpeedAndSize();
  }

  void updateSpeedAndSize() {
    if      (currentMood.equals("Calm"))     { radius = random(16,20); speed = random(0.5,1.5); }
    else if (currentMood.equals("Neutral"))  { radius = random(14,16); speed = random(1.5,2.5); }
    else if (currentMood.equals("Stressed")) { radius = random(8,12);  speed = random(3.0,5.0); }
    else                                     { radius = random(4,7);   speed = random(5.0,8.0); }
  }

  void update() {
    float noiseVal  = noise(x*0.003, y*0.003, noiseOffset + frameCount*0.005);
    float baseAngle = noiseVal * TWO_PI * 2;
    float chaos     = random(-PI, PI) * turbulence;
    angle = lerp(angle, baseAngle + chaos, 0.1 + turbulence * 0.3);
    x += cos(angle) * speed;
    y += sin(angle) * speed;
    if (x < 0) x = width;  if (x > width)  x = 0;
    if (y < 0) y = height; if (y > height) y = 0;
    if (random(1) < 0.02) colorIndex = int(random(8));
    if (frameCount % 60 == 0) updateSpeedAndSize();
  }

  void draw() {
    int[] c = getBlendedColor(colorIndex);
    float opacity = map(turbulence, 0, 1, 60, 180);
    noStroke();
    fill(c[0], c[1], c[2], opacity);
    ellipse(x, y, radius*2, radius*2);
  }
}

// ================================================================
// COLOUR HELPERS
// ================================================================
int[] getBlendedColor(int index) {
  int[] colA = palettes[currentPaletteIndex][index];
  int[] colB = palettes[targetPaletteIndex][index];
  return new int[]{
    (int)lerp(colA[0],colB[0],paletteBlend),
    (int)lerp(colA[1],colB[1],paletteBlend),
    (int)lerp(colA[2],colB[2],paletteBlend)
  };
}

void setMood(String mood) {
  if (mood.equals("Calibrating")) return;
  if (mood.equals(currentMood)) return;
  println("Mood changed: " + currentMood + " → " + mood);
  currentMood = mood;
  currentPaletteIndex = targetPaletteIndex;
  paletteBlend = 0.0;
  if      (mood.equals("Calm"))       { targetPaletteIndex = 0; targetTurbulence = 0.0;  }
  else if (mood.equals("Neutral"))    { targetPaletteIndex = 1; targetTurbulence = 0.2;  }
  else if (mood.equals("Stressed"))   { targetPaletteIndex = 2; targetTurbulence = 0.55; }
  else if (mood.equals("HighStress")) { targetPaletteIndex = 3; targetTurbulence = 1.0;  }
}

// ================================================================
// HUD
// ================================================================
void drawHUD() {
  // ---- LEFT PANEL — vitals (original) ----
  noStroke();
  fill(255, 255, 255, 200);
  rect(10, height - 65, 340, 58, 6);

  fill(30, 30, 30, 230);
  textSize(13);
  textAlign(LEFT);
  text("BPM: " + nf(currentBPM, 0, 1), 20, height - 48);
  text("RMSSD: " + nf(currentRMSSD, 0, 1) + "ms", 20, height - 32);
  text("MOOD: " + currentMood + "    TURBULENCE: " + nf(turbulence, 0, 2), 20, height - 16);

  // ---- RIGHT PANEL — debug log ----
  int rw = 400, rh = 120;
  int rx = width - rw - 10, ry = height - rh - 10;

  noStroke();
  fill(255, 255, 255, 200);
  rect(rx, ry, rw, rh, 6);

  // Signal dot
  float pulse = 0.5 + 0.5 * sin(frameCount * 0.15);
  int dotAlpha = signalStrength > 0.05 ?
    (int)(180 * signalStrength + 75 * pulse * signalStrength) : 60;
  fill(signalStrength > 0.05 ? color(40,180,80,dotAlpha) : color(180,50,50,80));
  ellipse(rx + rw - 16, ry + 14, 8, 8);

  fill(30, 30, 30, 200);
  textSize(10);
  textAlign(LEFT);
  text("UDP LOG  |  packets: " + packetCount + "  fps: " + nf(frameRate,0,0), rx+10, ry+18);

  stroke(180, 180, 180, 160);
  strokeWeight(1);
  line(rx+10, ry+24, rx+rw-10, ry+24);
  noStroke();

  // Last 5 packets, most recent on top, older ones fade
  for (int i = 0; i < 5; i++) {
    int idx = ((debugLogIndex - 1 - i) % 5 + 5) % 5;
    String entry = debugLog[idx];
    if (entry == null || entry.equals("")) continue;
    float fade = map(i, 0, 4, 220, 60);
    fill(30, 30, 30, (int)fade);
    textSize(10);
    if (entry.length() > 52) entry = entry.substring(0, 52) + "…";
    text(entry, rx+10, ry+40 + i*17);
  }
}

// ================================================================
// KEYBOARD
// ================================================================
void keyPressed() {
  if (key == '1') setMood("Calm");
  if (key == '2') setMood("Neutral");
  if (key == '3') setMood("Stressed");
  if (key == '4') setMood("HighStress");
  if (key == 'r') background(255, 255, 255);
  if (key == 's') saveFrame("MoodPainting-####.png");
  if (key == ESC) exit();
}
