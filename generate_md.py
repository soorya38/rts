import os

print("## Extracted Sprites Mapping")
print("")
for r in range(6):
    print(f"### Row {r}")
    print("<table><tr>")
    for c in range(10):
        f = f"assets/extracted/sprite_r{r}_c{c}.png"
        if os.path.exists(f):
            print(f"<td><img src='/Users/sooryaakilesh/test/rts/{f}' width='100'><br>r{r}_c{c}</td>")
    print("</tr></table>")
