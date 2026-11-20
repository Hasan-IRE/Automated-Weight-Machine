const express=require("express"),fs=require("fs"),path=require("path"),crypto=require("crypto");
const app=express(),port=process.env.PORT||3000;
const up=path.join(__dirname,"uploads"),db=path.join(__dirname,"measurements.json");
fs.mkdirSync(up,{recursive:true});if(!fs.existsSync(db))fs.writeFileSync(db,"[]");
app.use(express.static(path.join(__dirname,"public")));app.use("/uploads",express.static(up));
const read=()=>{try{return JSON.parse(fs.readFileSync(db,"utf8"))}catch{return []}};
app.get("/api/health",(_q,s)=>s.json({ok:true}));
app.get("/api/measurements",(_q,s)=>s.json(read().sort((a,b)=>new Date(b.timestamp)-new Date(a.timestamp))));
app.post("/api/upload-image",express.raw({type:"image/jpeg",limit:"8mb"}),(req,res)=>{
 if(!Buffer.isBuffer(req.body)||req.body.length<4)return res.status(400).json({error:"Expected JPEG body"});
 const id=String(req.get("X-Measurement-ID")||crypto.randomUUID()).replace(/[^a-zA-Z0-9_-]/g,"_");
 const weight=Number(req.get("X-Weight-Grams"));if(!Number.isFinite(weight))return res.status(400).json({error:"Invalid weight"});
 const file=id+".jpg";fs.writeFileSync(path.join(up,file),req.body);
 const record={measurementId:id,weightGrams:weight,weightKg:weight/1000,imageUrl:"/uploads/"+file,timestamp:new Date().toISOString(),stabilityStatus:"stable",captureStatus:"saved",deviceId:"smart-scale-01"};
 const all=read(),i=all.findIndex(x=>x.measurementId===id);if(i>=0)all[i]=record;else all.push(record);
 fs.writeFileSync(db,JSON.stringify(all,null,2));res.status(201).json({ok:true,measurement:record});
});
app.listen(port,"0.0.0.0",()=>console.log(`Dashboard/backend: http://localhost:${port}`));
