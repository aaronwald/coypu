import urllib.request, json, sys
data = json.load(sys.stdin)
for x in data:
    print (x['id'])
