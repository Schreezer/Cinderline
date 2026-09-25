#!/bin/bash
export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH
cd "$(dirname "$0")/../public/audio/takes"
for f in *.wav; do
  n="${f%.wav}"
  [ -f "$n.words.json" ] && continue
  mkdir -p "tx-$n"; cp "$f" "tx-$n/a.wav"
  npx --yes hyperframes@0.8.63 transcribe "tx-$n/a.wav" --engine whisper --model small.en --language en --json > "tx-$n/log.txt" 2>&1
  t=$(ls tx-$n/*.json 2>/dev/null | head -1); [ -n "$t" ] && cp "$t" "$n.words.json"
  ls "tx-$n" >> analyze.log
done
/usr/bin/python3 - << 'PY'
import json,glob
for f in sorted(glob.glob('*.words.json')):
    w=json.load(open(f)); w=w if isinstance(w,list) else w.get('words',w)
    txt=[x['text'].lower().strip('.,?!') for x in w]
    def gap_after(word,occ=1):
        c=0
        for i,t in enumerate(txt):
            if t==word:
                c+=1
                if c==occ and i+1<len(w): return round(w[i+1]['start']-w[i]['end'],2)
    # pause after last "proof", after "them", after "questions"/"proof?" first
    print(f.replace('.words.json',''), 'words',len(w),'end',round(w[-1]['end'],2),
      '| after proof1',gap_after('proof',1),'| after proof2',gap_after('proof',2),'| after them',gap_after('them'),
      '| after laxative',gap_after('laxative'),'| after toxins',gap_after('toxins'))
    print('   ',' '.join(x['text'] for x in w))
PY
