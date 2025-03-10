/*******************************************************************************
 *                                  sha256.cpp                                 *
 *                              Author: Fudmottin                              *
 *                                                                             *
 * This software is provided 'as-is', without any express or implied warranty. *
 * In no event will the authors be held liable for any damages arising from    *
 * the use of this software.                                                   *
 *                                                                             *
 * Permission is hereby granted, free of charge, to any person obtaining a     *
 * copy of this software and associated documentation files (the "Software"),  *
 * to deal in the Software without restriction, including without limitation   *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense and *
 * or sell copies of the Software.                                             *
 *                                                                             *
 *                   SHA-256 As defined by NIST.FIPS.180-4                     *
 *                     A great visualizer can be found at                      *
 *                                                                             *
 *                        https://sha256algorithm.com                          *
 *                                                                             *
 *              This file has been placed into The Public Domain               *
 *                                                                             *
 ******************************************************************************/

#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include "Word.h"

using namespace std;

// Convinience type to allow accessing vector elements using C style array
// accessor notation, eg a[i].
template<typename T>
class Vec : public std::vector<T> {
public:
    using vector<T>::vector;

    T& operator[](int i)
    { return vector<T>::at(i); }

    const T& operator[](int i) const
    { return vector<T>::at(i); }
};

// Type aliases to match the wording in the NIST.FIPS.180-4 SHA-256 specification.
using SHA256_Constants = array<Word,64>;
using Digest = array<Word, 8>;
using Message = Vec<unsigned char>;
using Block = array<Word,16>;
using Schedule = array<Word, 64>;

// Section 4.4.2 SHA-256 Constants
//
// These words represent the first thirty-two bits of the fractional parts of
// the cube roots of the first sixty-four prime numbers. In hex, these constant
// words are (from left to right)

static const SHA256_Constants K = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

// Section 5.3.3 SHA-256
//
// For SHA-256, the initial hash value, H(0), shall consist of the following 
// eight 32-bit words, in hex. These words were obtained by taking the first
// thirty-two bits of the fractional parts of the square roots of the first
// eight prime numbers.

static const Digest H0 = {
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

// Section 4.1.2 SHA-256 Functions
//
// SHA-256 uses six logical functions, where each function operates on 32-bit
// words which are represented as x, y, and z, The result of each function is
// a new 32 bit word.

// The 'Ch' function: This is short for "choose" and given three inputs x, y, z
// returns bits from y where the corresponding bit in x is 1 and bits from z
// where the corresponding bit in x is 0.
inline Word Ch(Word x, Word y, Word z) {return (x&y)^((~x)&z);}             // 4.2

// The 'Maj' function: Short for "majority", this function takes three inputs
// x, y, z and for each bit index i if at least two of the bits xi, yi or zi
// are set to 1 then so is the result mi.
inline Word Maj(Word x, Word y, Word z) {return (x&y)^(x&z)^(y&z);}         // 4.3

// The sigma functions: These are defined as bitwise operations on their input
// word according to specific rules outlined in section 4 of NIST.FIPS.180-4.
// They are used as part of generating a message schedule from a block of input
// data when calculating a SHA-256 hash. The suffixes are the part of the
// specification that defines each sigma function.
inline Word sigma_4_4(Word x) {return x.rotr(2) ^ x.rotr(13) ^ x.rotr(22);} // 4.4
inline Word sigma_4_5(Word x) {return x.rotr(6) ^ x.rotr(11) ^ x.rotr(25);} // 4.5
inline Word sigma_4_6(Word x) {return x.rotr(7) ^ x.rotr(18) ^ (x >> 3);}   // 4.6
inline Word sigma_4_7(Word x) {return x.rotr(17) ^ x.rotr(19) ^ (x >> 10);} // 4.7

// 5.1 Padding The Message: The purpose of this padding is to ensure that the
// padded message is a multiple of 512 bits. Padding can be inserted before hash
// computation begins on a message, or at any other time during the hash computation
// prior to processing the block(s) that will contain the padding.
const Message pad(uint64_t l) {
    Message padding = {0x80};

    if (l == 0) {
        // A zero length message is an edge case, but it has to be dealt with.
        padding.resize(56,0);
    } else if (l % 512 == 0) {
        // This is our favorite case. The message is already a multiple of 512
        // bits in length.
        return padding = {};
    } else if (l % 512  > 440) {
        // This is an annoying case. The message requires padding and adding an
        // extra 512 bit block to the end. Pad remainder of block and add new block
        int k = 960 - (l % 1024 + 1);
        padding.resize(k/8 + 1, 0);
    } 
    else {
        // This is a typical case. We add a 1 bit and zeros plus the length
        // of the message in bits.
        int k = 448 - (l % 512 + 1);
        padding.resize(k/8 + 1, 0);
    }

    // For some reason, unions are discouraged in C++. However, this approach does
    // work and I am lazy. Yes, bad_wolf is a Doctor Who reference.
    union {
        uint64_t m;
        unsigned char b[8];
    } bad_wolf;

    bad_wolf.m = l;

    // Reverse byte order for little endian machines like x86 and Apple Si. Big
    // endian architectures are not considered in this code for sake of simplicity.
    for (int i = 7; i > -1; i--) padding.push_back(bad_wolf.b[i]);

    return padding;
}

// 6.2.2 SHA-256 Hash Computation:
// The message is read into 512 bit (16 word) blocks which are in turn used
// to create a 64 word schedule. Each word in the schedule is referred to
// as Wt where t is from 0 to 63 inclusive. The schedule is the heart of
// the algorithm as it is used to modify the initial hash value (H0) and
// then each of the intermendiate digests produced when processing each
// block.
Schedule schedule(const Block& M) {
    Schedule W = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    int t = 0;
    do {
        W[t] = M[t];
        t++;
    } while(t < 16);
    do {
        Word w = sigma_4_7(W[t-2]) + W[t-7] + sigma_4_6(W[t-15]) + W[t-16];
        W[t] = w;
        t++;
    } while (t < 64);

    return W;
}

// 6.2.2 SHA-256 Hash Computation:
// Run the message schedule. This does the work of producing the next
// digest value from the current digest.
Digest runschedule(const Schedule& W, Digest& H) {
    Word a(H[0]), b(H[1]), c(H[2]), d(H[3]),
         e(H[4]), f(H[5]), g(H[6]), h(H[7]);

    for (int t = 0; t < 64; t++) {
        Word T1(h + sigma_4_5(e) + Ch(e,f,g) + K[t] + W[t]);
        Word T2(sigma_4_4(a) + Maj(a,b,c));
        h = g; g = f; f = e; e = d + T1; d = c; c = b; 
        b = a; a = T1 + T2;
    }

    H[0] = a + H[0];
    H[1] = b + H[1];
    H[2] = c + H[2];
    H[3] = d + H[3];
    H[4] = e + H[4];
    H[5] = f + H[5];
    H[6] = g + H[6];
    H[7] = h + H[7];

    return H;
}

// This implementation processes the message in memory. For small
// messages, that's fine. For larger messages, you would want to
// use a slightly more complex method that keeps track of the
// message size in bits as the blocks are read in. That would
// also affect how the padding is done as it has to tack data
// onto the end of the message so that it is an integer multiple
// of 512 bits (16 words).
Digest message(Message& msg) {
    uint64_t  messagelength = msg.size() * 8;
    Digest digest = H0; // The initial digest value is set.

    // The message padding is calculated and stored.
    const Message padding = pad(messagelength);

    // The padding is added on to the end of the message.
    for (auto e : padding) msg.push_back(e);

    // Parse the message 64 bytes at a time and process each block.
    int i = 0, j = 0;
    do {
        Block B = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        Word w = 0;

        do {
            unsigned char a, b, c, d;
            a = msg[i++]; b = msg[i++]; c = msg[i++]; d = msg[i++];
            w = w | a; w <<= 8;
            w = w | b; w <<= 8;
            w = w | c; w <<= 8;
            w = w | d;
            B[j] = w;
            w = 0;
            j++;
        } while (j < 16);

        Schedule s = schedule(B);
        digest = runschedule(s, digest);
        j = 0;
    } while (i < msg.size());

    return digest;
}

// This is a convenience function. Bitcoin uses sha256(sha256(data)).
// Since digests are a fixed 256 bit length, we already know the padding.
Digest hashDigest(const Digest& d) {
    Digest digest = H0;
    const Digest pad = {0x80000000,0x00000000,0x00000000,0x00000000,
                        0x00000000,0x00000000,0x00000000,0x00000200};
    Block B = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    int i = 0;
    for (auto w : d) B[i++] = w;
    for (auto w : pad) B[i++] = w;

    Schedule s = schedule(B);
    return runschedule(s, digest);
}

// This is just a simple utility function to parse the comman line
// arguments into a vector<string> type.
vector<string> arguments(int argc, char* argv[]) {
    vector<string> res;

    for (int i = 1; i < argc; i++)
        res.push_back(argv[i]);

    return res;
}

// This implementation reads each file to be hashed into memory. This
// works just fine for small files. Large files should be processed
// by streaming the data which would change all the code above. In
// practice, one would use a library function or utility like sha2
// to calculate the hash/digest of a file. This is just an educational
// example for acedemic purposes only.
int main(int argc, char* argv[]) {
    try {
        vector<string> args = arguments(argc, argv);

        if (argc == 1) {
            cout << "SHA-256 algorithm for educational purposes only!\n"
                 << "$ sha256 [-] file1 [file2 ...]\n\n"
                 << "Reads each file and provides a SHA-256 digest.\n"
                 << "The - argument can appear anywhere in the argument\n"
                 << "list. Files appearing after the - will be double hashed.\n"
                 << "Bitcoin does this sha256(sha256(data)).\n"
                 << "The output is a text hex representation of the "
                 << "SHA-256 message digest.\n";
            return 0;
        }

        Message msg = {};
        msg.reserve(1024);

        bool doublehash = false;
        for (auto file : args) {
            char ch = 0;

            if (file == string("-")) {
                doublehash = true;
                continue;
            }

            ifstream infile(file, ios::binary);
            while (infile.read(&ch, 1))
                msg.push_back((unsigned char)ch);

            infile.close();

            Digest digest = message(msg);

            if (doublehash) digest = hashDigest(digest);

            if (doublehash) cout << " double hashed";

            cout << "SHA-256 (" << file << ") = ";
            for (auto w : digest)
                cout << setw(8) << setfill('0') << hex << w;
            cout << endl;

            msg = {};
        }
    }
    // Honestly if we catch an error, there is a bug somewhere in the
    // code that I have not caught. Pun intended.
    catch (out_of_range) {
        cerr << "range error" << endl;
    }
    catch (...) {
        cerr << "unknown exception thrown" << endl;
    }

    return 0;
}

