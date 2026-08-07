import os, re, urllib.request

outdir = os.path.dirname(os.path.abspath(__file__))
outdir = os.path.join(outdir, "refs")
os.makedirs(outdir, exist_ok=True)

pages = [
    "https://cnc.fandom.com/wiki/File:RA2_French_Grand_Cannon.jpg",
    "https://cnc.fandom.com/wiki/File:RA2_Grand_Cannon.png",
    "https://cnc.fandom.com/wiki/File:Grand_Cannon_in_Snow_Theater.jpg",
    "https://cnc.fandom.com/wiki/File:Grand_Cannon_animation.gif",
]

for page in pages:
    try:
        req = urllib.request.Request(page, headers={"User-Agent": "Mozilla/5.0"})
        html = urllib.request.urlopen(req, timeout=45).read().decode("utf-8", "replace")
        m = re.search(
            r"https://static\.wikia\.nocookie\.net/[^\"']+\.(?:jpg|png|gif|jpeg)/revision/latest[^\"']*",
            html,
            re.I,
        )
        url = m.group(0) if m else None
        print(page.split("/")[-1], "->", (url[:100] + "...") if url and len(url) > 100 else url)
        if url:
            url = url.split("/scale-to")[0]
            name = page.split(":")[-1].replace(" ", "_")
            path = os.path.join(outdir, name)
            urllib.request.urlretrieve(url, path)
            print("  saved", name, os.path.getsize(path))
    except Exception as e:
        print("FAIL", page, type(e).__name__, e)
