#!/usr/bin/env python3
"""Edit genuine RC3 captures. Usage: edit_rc3_promo.py RAW_DIR OUTPUT_DIR.
RAW_DIR: globe.mp4, map.mp4, map-wide.mp4, menu.mp4 (400x480 dual screen).
No synthetic application screens, speed changes, or interpolated frames.
"""
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
raw,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
work=Path(tempfile.mkdtemp(prefix='rc3-edit-'))
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
bold='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
BG='#081522';WHITE='#f0f5f5';TEAL='#71dce1';MUTED='#acc0cd'
def run(a):subprocess.run(a,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
def text(d,xy,s,size=24,color=WHITE,b=False):d.text(xy,s,font=ImageFont.truetype(bold if b else font,size),fill=color)
def bg(title,lines,kind,number,total,ending=False):
 im=Image.new('RGB',(1280,720),BG);d=ImageDraw.Draw(im)
 text(d,(40,25),'MAPS FOR NINTENDO 3DS',18,TEAL,True)
 text(d,(40,65),title,40,WHITE,True)
 d.rounded_rectangle((36,156,844,644),radius=10,fill='#213747')
 d.rounded_rectangle((876,156,1204,404),radius=8,fill='#213747')
 text(d,(880,445),'RC3 PREVIEW' if ending else 'TAKE A LOOK',17,TEAL,True)
 for i,line in enumerate(lines):text(d,(880,481+i*30),line,21)
 text(d,(40,668),kind,18,MUTED)
 text(d,(1100,668),f'{number:02} / {total:02}',18,TEAL)
 return im
# (source,start,duration,title,side text,precise capture label)
G='RC3 / Azahar capture  •  NASA Blue Marble'
M='RC3 / Azahar capture  •  Cached Esri imagery / contributors'
U='RC3 / Azahar capture  •  StreetGuess menu preview'
shots={
 'world':('globe',4.1,2.4,'Wait. This runs on a 3DS?', ['A globe you','can spin.'],G),
 'city':('map',3.4,1.7,'Start somewhere interesting.', ['Lower Manhattan.','Pan. Zoom. Explore.'],M),
 'close':('map',5.6,2.0,'Get a little closer.', ['Explore the city','on two screens.'],M),
 'spin':('globe',.7,3.0,'Take the long way around.', ['Turn the globe.','Find your next stop.'],G),
 'daily':('menu',2.0,1.2,'A new challenge to choose.', ['StreetGuess','Daily challenge UI'],U),
 'friends':('menu',5.0,1.2,'Pass the console.', ['StreetGuess','Local play setup','2–4 players'],U),
 'mystery':('menu',7.5,1.6,'Where next?', ['StreetGuess','Mystery Expedition UI'],U),
 'wide':('map-wide',2.5,2.2,'A different view of the city.', ['Keep exploring.','From above.'],M),
 'back':('map',6.5,2.0,'The next place is up to you.', ['Maps. A globe.','A geography game.'],M),
 'end':('globe',4.1,3.0,'Explore the world. Pass the console.', ['Try the RC3 preview.','Downloads + FBI QR','in the README.'],G),
}
teaser=['world','city','close','spin','daily','friends','mystery','wide','back','end']
demo=['world','city','wide','close','spin','daily','friends','mystery','back','end']
def video(name,seq,long=False):
 files=[];timeline=[];at=0
 for i,k in enumerate(seq):
  src,start,dur,title,lines,label=shots[k]
  if long:
   dur={'world':3.0,'city':3.6,'wide':3.0,'close':2.8,'spin':5.2,'daily':2.6,'friends':2.6,'mystery':2.2,'back':2.8,'end':4.2}[k]
   if k=='city':start=6.2 # clean closer city frame, no incomplete tile flash
   if k=='daily':
    start=2.0;title='Daily or a friend’s challenge?';lines=['StreetGuess','Daily + friend UI']
   if k=='friends':
    start=5.0;title='Choose how to play.';lines=['StreetGuess','Pass and play.','Or Surprise Me.']
   if k=='mystery':start=7.5
  lines=list(lines)
  if k=='end':lines=['Try the RC3 preview.','CIA + 3DSX + FBI QR','in the README.']
  p=work/f'{name}-{i}.png';bg(title,lines,label,i+1,len(seq),k=='end').save(p)
  dest=work/f'{name}-{i}.mp4'
  filt='[1:v]split=2[t][b];[t]crop=400:240:0:0,scale=800:480:flags=lanczos[top];[b]crop=320:240:40:240[bottom];[0:v][top]overlay=40:160[c];[c][bottom]overlay=880:160,format=yuv420p[v]'
  run(['ffmpeg','-v','error','-y','-loop','1','-framerate','30','-i',str(p),'-ss',str(start),'-i',str(raw/f'{src}.mp4'),'-filter_complex',filt,'-map','[v]','-t',str(dur),'-an','-c:v','libx264','-preset','fast','-crf','20','-threads','2',str(dest)])
  timeline.append({'source':src+'.mp4','in':start,'duration':dur,'title':title,'capture_label':label,'output_in':round(at,2)});at+=dur;files.append(dest)
 listing=work/f'{name}.txt';listing.write_text(''.join("file '"+str(p)+"'\n" for p in files))
 run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(listing),'-c','copy','-movflags','+faststart',str(out/f'{name}.mp4')])
 return timeline
edits={'teaser':video('teaser',teaser),'review-walkthrough':video('review-walkthrough',demo,True)}
# Compact looping hero: real frames, three capture categories, no menu wall.
hero=[('globe',4.1,2.3,'SPIN THE WORLD','NASA Blue Marble'),('map',3.4,1.7,'EXPLORE MANHATTAN','Cached Esri imagery'),('menu',5.0,1.4,'PASS THE CONSOLE','StreetGuess menu preview'),('map',6.3,2.0,'TAKE A CLOSER LOOK','Cached Esri imagery'),('globe',1.2,2.0,'WHERE NEXT?','NASA Blue Marble')]
parts=[]
for i,(src,start,dur,title,label) in enumerate(hero):
 p=work/f'hero-{i}.png';im=Image.new('RGB',(400,540),BG);d=ImageDraw.Draw(im)
 text(d,(16,8),title,18,TEAL,True);text(d,(16,516),'RC3 / Azahar  •  '+label,11,MUTED);im.save(p)
 dest=work/f'hero-{i}.mp4';run(['ffmpeg','-v','error','-y','-loop','1','-framerate','30','-i',str(p),'-ss',str(start),'-i',str(raw/f'{src}.mp4'),'-filter_complex','[0:v][1:v]overlay=0:32,format=yuv420p[v]','-map','[v]','-t',str(dur),'-an','-c:v','libx264','-crf','18','-threads','2',str(dest)]);parts.append(dest)
listing=work/'hero.txt';listing.write_text(''.join("file '"+str(p)+"'\n" for p in parts))
run(['ffmpeg','-v','error','-y','-f','concat','-safe','0','-i',str(listing),'-filter_complex','fps=12,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a','-loop','0',str(out/'hero.gif')])
for name,src,t in [('map-close','map',8),('globe','globe',5.2),('daily-menu','menu',2.6),('local-play-menu','menu',5)]:
 run(['ffmpeg','-v','error','-y','-ss',str(t),'-i',str(raw/f'{src}.mp4'),'-frames:v','1',str(out/f'{name}.png')])
manifest={'source_commit':'9e09e27a12d88dee8de5aca5983f5d4a245b5999','package_commit':'9cc4858b5fca0cb8d3806ead7ed0b960c7f144fb','sources':[{ 'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(raw.glob('*.mp4'))],'speed':1,'audio':'silent','teaser':edits['teaser'],'demo':edits['review-walkthrough'],'hero':[dict(source=s+'.mp4',start=t,duration=d,caption=c,attribution=a) for s,t,d,c,a in hero],'excluded':'No live panorama, panorama detail, terrain/building capture, guessed round or score screen was successfully captured. No diagnostic build footage is used.'}
(out.parent.parent/'release/rc3/promo-edit.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Rendered teaser, demo, loop and four screenshots to',out)
