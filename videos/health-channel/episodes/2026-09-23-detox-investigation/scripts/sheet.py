import sys,glob
from PIL import Image
paths=sys.argv[2:];W=int(sys.argv[1]);H=int(W*16/9)
cols=min(3,len(paths));rows=(len(paths)+cols-1)//cols
s=Image.new('RGB',(cols*W,rows*H),'white')
for i,p in enumerate(paths):
    s.paste(Image.open(p).convert('RGB').resize((W,H)),(i%cols*W,i//cols*H))
s.save('/tmp/sheet.jpg',quality=50)
