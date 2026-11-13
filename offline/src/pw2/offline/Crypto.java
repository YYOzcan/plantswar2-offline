package pw2.offline;

import java.io.ByteArrayOutputStream;
import java.io.UnsupportedEncodingException;
import java.util.Random;

/** Plants War 2 transport cipher (port of server/pw2crypto.py, reversed from libGame.so). */
final class Crypto {
    private Crypto() {}

    private static final Random RNG = new Random();

    static int swap(int b) { return ((b & 0xF) << 4) | ((b >> 4) & 0xF); }

    private static int nextStep(int s) { return s == 0xFD ? 0 : s == 0xFE ? 1 : s + 2; }

    private static long cksumAdd(long c, int b) {
        if (c > 0x7FFFFEFEL) c = 0;
        return c + b;
    }

    static byte[] encodeRaw(byte[] plain, int[] key) {
        if (key == null) {
            key = new int[8];
            for (int j = 0; j < 8; j++) key[j] = RNG.nextInt(0xF0) + 0x10;
        }
        byte[] out = new byte[12 + plain.length];
        long c = 0; int st = 0, s = 0x10;
        for (int i = 0; i < plain.length; i++) {
            int p = plain[i] & 0xFF, k = key[i % 8], x = p ^ s, o;
            c = cksumAdd(c, p);
            if (st == 0 || st == 4) { o = swap(x) ^ k; st = 1; }
            else if (st == 1) { o = swap(swap(x) ^ k); st = 2; }
            else if (st == 2) { o = swap(x ^ k); st = 3; }
            else { o = x ^ k; st = 4; }
            out[12 + i] = (byte) o;
            s = nextStep(s);
        }
        for (int j = 0; j < 8; j++) out[j] = (byte) swap(key[j] ^ 0x69);
        out[8] = (byte) ((c & 0xFF) ^ 0x2F ^ key[0] ^ key[4]);
        out[9] = (byte) (((c >> 8) & 0xFF) ^ 0xB3 ^ key[1] ^ key[5]);
        out[10] = (byte) (((c >> 16) & 0xFF) ^ 0x58 ^ key[2] ^ key[6]);
        out[11] = (byte) (((c >> 24) & 0xFF) ^ 0xD8 ^ key[3] ^ key[7]);
        return out;
    }

    static byte[] decodeRaw(byte[] d) {
        if (d.length < 12) return new byte[0];
        int[] key = new int[8];
        for (int j = 0; j < 8; j++) key[j] = swap(d[j] & 0xFF) ^ 0x69;
        byte[] out = new byte[d.length - 12];
        int st = 0, s = 0x10;
        for (int i = 0; i < out.length; i++) {
            int b = d[12 + i] & 0xFF, k = key[i % 8], v;
            if (st == 0 || st == 4) { v = swap(b ^ k); st = 1; }
            else if (st == 1) { v = swap(swap(b) ^ k); st = 2; }
            else if (st == 2) { v = swap(b) ^ k; st = 3; }
            else { v = b ^ k; st = 4; }
            out[i] = (byte) (v ^ s);
            s = nextStep(s);
        }
        return out;
    }

    private static final char[] B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();

    static String b64e(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < b.length; i += 3) {
            int n = (b[i] & 0xFF) << 16;
            if (i + 1 < b.length) n |= (b[i + 1] & 0xFF) << 8;
            if (i + 2 < b.length) n |= b[i + 2] & 0xFF;
            sb.append(B64[(n >> 18) & 63]).append(B64[(n >> 12) & 63]);
            sb.append(i + 1 < b.length ? B64[(n >> 6) & 63] : '=');
            sb.append(i + 2 < b.length ? B64[n & 63] : '=');
        }
        return sb.toString().replace('+', '-');
    }

    static byte[] b64d(String s) {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        int acc = 0, bits = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+' || c == '-' || c == ' ') v = 62;
            else if (c == '/') v = 63;
            else continue;   // '=', whitespace
            acc = (acc << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; o.write((acc >> bits) & 0xFF); }
        }
        return o.toByteArray();
    }

    static String decode(String s) {
        try { return new String(decodeRaw(b64d(s)), "UTF-8"); }
        catch (UnsupportedEncodingException e) { throw new RuntimeException(e); }
    }

    static String encode(String plain) {
        try { return b64e(encodeRaw(plain.getBytes("UTF-8"), null)); }
        catch (UnsupportedEncodingException e) { throw new RuntimeException(e); }
    }
}
