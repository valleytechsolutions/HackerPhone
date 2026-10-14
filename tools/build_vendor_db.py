"""Build optional HackerPhone OUI data from a user-supplied IEEE MA-L CSV.

No network requests, GPL firmware, flash-filesystem format, or serial writes.
Keep the source registry's terms/provenance with the generated SD file.
"""
import argparse,csv,hashlib,json,pathlib,struct,unicodedata

def build(source, destination):
    rows={}
    with source.open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f):
            assignment=row.get('Assignment','').strip()
            if len(assignment)!=6 or any(c not in '0123456789abcdefABCDEF' for c in assignment):
                continue
            name=unicodedata.normalize('NFKD',row.get('Organization Name','')).encode('ascii','ignore').decode()
            name=' '.join(name.split())[:47]
            if name: rows[int(assignment,16)]=name
    if not rows or len(rows)>100000: raise ValueError('Expected 1..100000 MA-L assignments')
    data=b'HPOUI1\0\0'+struct.pack('<II',len(rows),52)
    data+=b''.join(struct.pack('<I48s',key,name.encode()) for key,name in sorted(rows.items()))
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(data)
    destination.with_suffix('.json').write_text(json.dumps({
        'format':'HPOUI1','entries':len(rows),'source_filename':source.name,
        'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'database_sha256':hashlib.sha256(data).hexdigest(),
        'note':'Registry data retains its original terms; vendor assignment is not a device identification.'
    },indent=2)+'\n')
    return len(rows)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('ieee_csv',type=pathlib.Path);p.add_argument('output',type=pathlib.Path)
    args=p.parse_args();print(f'Built {build(args.ieee_csv,args.output)} vendor records: {args.output}')
