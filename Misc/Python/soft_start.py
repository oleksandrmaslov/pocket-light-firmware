def make_fade_curve(steps=128, gamma=2.2):
    curve = []
    for i in range(steps):
        x = i / (steps - 1)
        val = round((x ** gamma) * 100)  # 0..100
        curve.append(val)
    return curve

if __name__ == "__main__":
    vals = make_fade_curve(steps=128, gamma=2.2)
    print(", ".join(map(str, vals)))
