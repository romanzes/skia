
out vec4 sk_FragColor;
uniform int i;
uniform int j;
void main() {
    bool x = bool(i);
    bool y = bool(j);
    bool andXY = x && y;
    bool orXY = x || y;
    bool combo = x && y || (x || y);
    bool prec = i + j == 3 && y;
    while (((andXY && orXY) && combo) && prec) {
        sk_FragColor = vec4(0.0);
        break;
    }
}
