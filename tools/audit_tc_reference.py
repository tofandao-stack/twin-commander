#!/usr/bin/env python3
"""Extract functional UI metadata from a local, lawfully obtained TC reference.

No executable code, icons, bitmaps or artwork are emitted. Inputs are never run.
Usage: audit_tc_reference.py TOTALCMD64.EXE DEFAULT.BAR output.json
"""
import sys,struct,json,hashlib,configparser,re
from pathlib import Path
class Reader:
 def __init__(self,b,o):self.b=b;self.o=o
 def take(self,n):v=self.b[self.o:self.o+n];self.o+=n;return v
 def byte(self):return self.take(1)[0]
 def short(self):return self.take(self.byte()).decode('utf-8',errors='replace')
 def integer(self,fmt):return struct.unpack(fmt,self.take(struct.calcsize(fmt)))[0]
 def value(self):
  t=self.byte()
  if t in (0,13):return None
  if t==1:
   v=[]
   while self.b[self.o]!=0:v.append(self.value())
   self.byte();return v
  if t in (2,3,4,15,16,17,19,21):return self.integer({2:'<b',3:'<h',4:'<i',15:'<f',16:'<q',17:'<d',19:'<q',21:'<d'}[t])
  if t==5:return self.take(10).hex()
  if t in (6,7):return self.short()
  if t in (8,9):return t==9
  if t==10:
   self.take(self.integer('<I'));return '<binary artwork omitted>'
  if t==11:
   v=[]
   while self.b[self.o]!=0:v.append(self.short())
   self.byte();return v
  if t==14:
   items=[]
   while self.b[self.o]!=0:
    if self.byte()!=1: raise ValueError(('collection start',self.o))
    item={}
    while self.b[self.o]:
     k=self.short();item[k]=self.value()
    self.byte();items.append(item)
   self.byte();return items
  if t in (12,20):return self.take(self.integer('<I')).decode('utf-8',errors='replace')
  if t==18:return self.take(2*self.integer('<I')).decode('utf-16le')
  raise ValueError((t,self.o))
 def obj(self):
  if self.b[self.o]>=0xf0:
   flags=self.byte() & 15
   if flags & 2:self.value()
  cl=self.short();name=self.short();p={}
  while self.b[self.o]:
   k=self.short();p[k]=self.value()
  self.byte();children=[]
  while self.b[self.o]:children.append(self.obj())
  self.byte();return dict(type=cl,name=name,properties=p,children=children)

def chunks(binary, offset):
 parts=[]
 while True:
  if binary[offset-16:offset-8] != b'\xff'*8: raise ValueError('Unexpected FPC string header')
  length=struct.unpack_from('<Q',binary,offset-8)[0]
  if length>1024*1024: raise ValueError('Unexpected form chunk size')
  data=binary[offset:offset+length]
  if data==b'FORMDATA': return b''.join(parts)
  parts.append(data);offset=(offset+length+8)//8*8+16

def strings(binary):
 pe=struct.unpack_from('<I',binary,0x3c)[0]; count=struct.unpack_from('<H',binary,pe+6)[0]
 opt=pe+24; size=struct.unpack_from('<H',binary,pe+20)[0]
 magic=struct.unpack_from('<H',binary,opt)[0];dirs=opt+(112 if magic==0x20b else 96)
 rva=struct.unpack_from('<I',binary,dirs+16)[0]; sections=[]
 for i in range(count):
  off=opt+size+40*i;vsize,va,rsize,raw=struct.unpack_from('<IIII',binary,off+8);sections.append((va,max(vsize,rsize),raw))
 def physical(address):
  for va,size,raw in sections:
   if va<=address<va+size:return raw+address-va
  raise ValueError('Resource RVA outside sections')
 root=physical(rva)
 def directory(relative):
  off=root+relative;n=sum(struct.unpack_from('<HH',binary,off+12));return [struct.unpack_from('<II',binary,off+16+8*i) for i in range(n)]
 result={}
 for kind,child in directory(0):
  if kind!=6:continue
  for block,langs in directory(child&0x7fffffff):
   for lang,data in directory(langs&0x7fffffff):
    address,size=struct.unpack_from('<II',binary,root+data);off=physical(address)
    for i in range(16):
     length=struct.unpack_from('<H',binary,off)[0];off+=2
     result[(block-1)*16+i]=binary[off:off+2*length].decode('utf-16le');off+=2*length
 return result

def bar_slots(path):
 config=configparser.ConfigParser();config.read(path,encoding='utf-8-sig');bar=config['Buttonbar'];result=[]
 commands={}
 for line in Path(path).with_name('TOTALCMD.INC').read_text(encoding='latin1').splitlines():
  match=re.match(r'(cm_\w+)=(\d+);',line)
  if match:commands[match.group(1).casefold()]=int(match.group(2))
 for i in range(1,int(bar['Buttoncount'])+1):
  result.append({'slot':i,'command':bar.get('cmd'+str(i),''),'separator':not bar.get('button'+str(i),''),'referenceIcon':bar.get('button'+str(i),''),'nativeId':commands.get(bar.get('cmd'+str(i),'').casefold(),0)})
 return result

def extract(executable, buttonbar):
 binary=Path(executable).read_bytes(); offset=binary.find(b'TPF0\nTTOTAL_CMD')
 if offset<0: raise ValueError('Reference main form not found')
 stream=chunks(binary,offset); form=Reader(stream,4).obj()
 menu=next(c for c in form['children'] if c['type']=='TMainMenu')
 def item(node):
  p=node['properties'];result={'name':node['name'],'caption':p.get('Caption',''),'command':p.get('Tag',0)}
  if node['children']:result['items']=[item(c) for c in node['children']]
  if p.get('RightJustify'): result['rightAligned']=True
  return result
 text=strings(binary)
 toolbar=bar_slots(buttonbar)
 vertical=bar_slots(Path(buttonbar).with_name('VERTICAL.BAR'))
 return {'reference':{'product':'Total Commander','version':'11.58.1','architecture':'x64','language':'built-in English',
  'executableSha256':hashlib.sha256(binary).hexdigest(),'buttonbarSha256':hashlib.sha256(Path(buttonbar).read_bytes()).hexdigest()},
  'menus':[item(c) for c in menu['children']], 'toolbar':toolbar, 'verticalToolbar':vertical,
  'layoutDefaults':{'ButtonBar':1,'ButtonBarVertical':1,'CmdLine':1,'CurDir':1,'DirectoryTabs':1,'DriveBar1':0,'DriveBar2':0,'DriveCombo':1,'HistoryHotlistButtons':1,'InterfaceFlat':1,'KeyButtons':1,'StatusBar':1,'TabHeader':1,'alwaysShowTabs':0},
  'functionKeys':[text[i] for i in range(1072,1079)], 'columns':text[1140].split('\n'),
  'sourceForm':{'width':form['properties']['Width'],'height':form['properties']['Height'],'font':form['properties']['Font.Name'],'fontHeight':form['properties']['Font.Height']}}
if __name__=='__main__':
 if len(sys.argv)!=4:raise SystemExit(__doc__)
 Path(sys.argv[3]).write_text(json.dumps(extract(sys.argv[1],sys.argv[2]),ensure_ascii=False,indent=2)+'\n')
