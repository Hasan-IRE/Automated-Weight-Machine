const express = require("express"),
  fs = require("fs"),
  path = require("path"),
  crypto = require("crypto");

// Zero-dependency .env loader
function loadEnv() {
  const envPath = path.join(__dirname, ".env");
  if (fs.existsSync(envPath)) {
    const lines = fs.readFileSync(envPath, "utf8").split("\n");
    for (const line of lines) {
      const idx = line.indexOf("=");
      if (idx > 0 && !line.trim().startsWith("#")) {
        const k = line.substring(0, idx).trim();
        const v = line.substring(idx + 1).trim();
        process.env[k] = v;
      }
    }
  }
}
loadEnv();

const app = express(),
  port = process.env.PORT || 3000;
const up = path.join(__dirname, "uploads"),
  db = path.join(__dirname, "measurements.json");

fs.mkdirSync(up, { recursive: true });
if (!fs.existsSync(db)) fs.writeFileSync(db, "[]");

app.use(express.static(path.join(__dirname, "public")));
app.use("/uploads", express.static(up));

const SUPABASE_URL = process.env.SUPABASE_URL || "https://trrhhfnvwkozouwsvrkx.supabase.co";
const SUPABASE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY || process.env.SUPABASE_ANON_KEY;

// Sync record & image to Supabase Cloud Database & Storage
async function syncToSupabase(record, imageBuffer) {
  if (!SUPABASE_URL || !SUPABASE_KEY) return;
  try {
    let cloudImageUrl = record.imageUrl;

    // 1. Upload snapshot to Supabase Storage
    try {
      const storageRes = await fetch(
        `${SUPABASE_URL}/storage/v1/object/weight-images/${record.measurementId}.jpg`,
        {
          method: "POST",
          headers: {
            Authorization: `Bearer ${SUPABASE_KEY}`,
            "Content-Type": "image/jpeg",
          },
          body: imageBuffer,
        }
      );
      if (storageRes.ok) {
        cloudImageUrl = `${SUPABASE_URL}/storage/v1/object/public/weight-images/${record.measurementId}.jpg`;
        console.log(`[Supabase Storage] Image uploaded: ${cloudImageUrl}`);
      }
    } catch (e) {
      console.warn("[Supabase Storage] Upload error:", e.message);
    }

    // 2. Insert record into Supabase PostgreSQL Table 'measurements'
    const dbRes = await fetch(`${SUPABASE_URL}/rest/v1/measurements`, {
      method: "POST",
      headers: {
        apikey: SUPABASE_KEY,
        Authorization: `Bearer ${SUPABASE_KEY}`,
        "Content-Type": "application/json",
        Prefer: "return=representation",
      },
      body: JSON.stringify({
        id: record.measurementId,
        weight_grams: record.weightGrams,
        weight_kg: record.weightKg,
        image_url: cloudImageUrl,
        stability_status: record.stabilityStatus,
        capture_status: record.captureStatus,
        device_id: record.deviceId,
      }),
    });

    if (dbRes.ok) {
      console.log(`[Supabase Database] Successfully synced record ID: ${record.measurementId}`);
    } else {
      console.warn(`[Supabase Database] Insert status: ${dbRes.status} ${await dbRes.text()}`);
    }
  } catch (err) {
    console.error("[Supabase] Sync failed:", err.message);
  }
}

const read = () => {
  try {
    return JSON.parse(fs.readFileSync(db, "utf8"));
  } catch {
    return [];
  }
};

app.get("/api/health", (_q, s) =>
  s.json({
    ok: true,
    supabase: !!SUPABASE_KEY,
    supabaseUrl: SUPABASE_URL,
  })
);

app.get("/api/measurements", (_q, s) =>
  s.json(read().sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp)))
);

app.post(
  "/api/upload-image",
  express.raw({ type: "image/jpeg", limit: "8mb" }),
  (req, res) => {
    if (!Buffer.isBuffer(req.body) || req.body.length < 4)
      return res.status(400).json({ error: "Expected JPEG body" });

    const id = String(
      req.get("X-Measurement-ID") || crypto.randomUUID()
    ).replace(/[^a-zA-Z0-9_-]/g, "_");

    const weight = Number(req.get("X-Weight-Grams"));
    if (!Number.isFinite(weight))
      return res.status(400).json({ error: "Invalid weight" });

    const file = id + ".jpg";
    fs.writeFileSync(path.join(up, file), req.body);

    const record = {
      measurementId: id,
      weightGrams: weight,
      weightKg: weight / 1000,
      imageUrl: "/uploads/" + file,
      timestamp: new Date().toISOString(),
      stabilityStatus: "stable",
      captureStatus: "saved",
      deviceId: "smart-scale-01",
    };

    const all = read(),
      i = all.findIndex((x) => x.measurementId === id);
    if (i >= 0) all[i] = record;
    else all.push(record);

    fs.writeFileSync(db, JSON.stringify(all, null, 2));

    // Async sync to Supabase Cloud
    syncToSupabase(record, req.body).catch((e) =>
      console.error("[Supabase Async Error]", e)
    );

    res.status(201).json({ ok: true, measurement: record });
  }
);

app.listen(port, "0.0.0.0", () => {
  console.log(`\n=================================================`);
  console.log(`  Smart Weighing Backend & Dashboard Ready`);
  console.log(`  Local URL:    http://localhost:${port}`);
  console.log(`  Supabase URL: ${SUPABASE_URL}`);
  console.log(`=================================================\n`);
});
