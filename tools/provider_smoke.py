"""Small opt-in live-service check; no gameplay or console-performance claims.
Run manually, never in CI. No imagery is retained. No Google/Bing scraping.
"""
import io,json,math,pathlib,subprocess,tempfile,time
from PIL import Image
checks=[('NASA Blue Marble','https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/BlueMarble_NextGeneration/default/GoogleMapsCompatible_Level8/2/1/1.jpeg','image')]
for name,lat,lon in [('New York city',40.71,-74.01),('Alps mountains',45.92,6.87),('Atlantic coastline',48.39,-4.49),('Kansas flat land',38.5,-98.0),('Tokyo city',35.68,139.76)]:
 z=12;n=1<<z;x=int((lon+180)/360*n);y=int((1-math.asinh(math.tan(math.radians(lat)))/math.pi)*n/2)
 checks.append((name,f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png','image'))
root=pathlib.Path(__file__).resolve().parents[1]
loc=json.loads((root/'docs/datasets/streetguess-v1.json').read_text())['locations'][0]
checks.append(('Panoramax exact STAC item',loc['metadata_url'],'json'))
results=[]
with tempfile.TemporaryDirectory() as temp:
 for name,url,kind in checks:
  path=pathlib.Path(temp)/'response';start=time.monotonic()
  p=subprocess.run(['curl','--fail','--silent','--show-error','--location','--max-time','20','--user-agent','3DS-Google-Maps/2.2 validation (+https://github.com/Oldhimaster1/3ds-Google-Maps)',url,'-o',str(path)],capture_output=True)
  r={'sample':name,'host_seconds':round(time.monotonic()-start,3),'pass':False}
  try:
   if p.returncode:raise RuntimeError(f'curl exit {p.returncode}')
   b=path.read_bytes();r['bytes']=len(b)
   if kind=='image':
    im=Image.open(io.BytesIO(b));im.load();r['dimensions']=[im.width,im.height];assert im.size==(256,256)
   else:assert json.loads(b)['id']==loc['id']
   r['pass']=True
  except Exception as e:r['error']=str(e)
  results.append(r);print(json.dumps(r),flush=True)
(root/'docs/release/rc1/provider-smoke.json').write_text(json.dumps({'date_utc':'2026-11-11','environment':'Desktop host, not 3DS; host TLS trust store. Timing is not console performance.','checks':results},indent=2)+'\n')
