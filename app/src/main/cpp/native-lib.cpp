#include <jni.h>
#include <string>
#define RC5_ENC_BLOCK_SIZE  4

typedef unsigned short WORD;
#define _wordLengthInBit sizeof(WORD) * 8//w
#define _round 12 //r
#define _keyLengthInByte 16 //b
#define _keyLengthInWord 8//_keyLengthInByte * 8 / _wordLengthInBit; //c: key length in
#define _sTableSize 26 //2 * (_round + 1); // t: S table size
WORD _S[_sTableSize]; //S table
WORD CyclicRightShift(WORD i, WORD a);

WORD CyclicLeftShift(WORD i, WORD i1);

const WORD _P = (WORD)0xb7e1; //magic constants
const WORD _Q = (WORD)0x9e37; //magic constants
WORD CyclicRightShift(WORD x, WORD y)
{
    return (x >> (y&(_wordLengthInBit - 1))) | (x << (_wordLengthInBit - (y&(_wordLengthInBit - 1))));
}

WORD CyclicLeftShift(WORD x, WORD y)
{
    return (x << (y&(_wordLengthInBit - 1))) | (x >> (_wordLengthInBit - (y&(_wordLengthInBit - 1))));
}
void cipher_rc5_setup(unsigned char *keyData)
{
    int i = 0, j= 0, k =0;
    WORD A = 0, B = 0;
    WORD u = _wordLengthInBit / 8;
    WORD L[_keyLengthInWord];
    memset(L, 0, sizeof(L));
    for (i = _keyLengthInByte - 1; i != -1; i--)
        L[i / u] = (L[i / u] << 8) + keyData[i];
    for (i = 1, _S[0] = _P; i < _sTableSize; i++)
        _S[i] = _S[i - 1] + _Q;
    for (A = B = i = j = k = 0; k < 3 * _sTableSize; k++)
    {
        A = _S[i] = CyclicLeftShift(_S[i] + (A + B), 3);
        B = L[j] = CyclicLeftShift(L[j] + (A + B), A + B);
        i = (i + 1) % _sTableSize;
        j = (j + 1) % _keyLengthInWord;
    }
}



WORD* cipher_rc5_encrypt(const WORD *pt)
{
    int i = 0;
    WORD ct[2];
    WORD A = pt[0] + _S[0], B = pt[1] + _S[1];
    for (i = 1; i <= _round; i++)
    {
        A = CyclicLeftShift((WORD)(A^B), B) + _S[2 * i];
        B = CyclicLeftShift((WORD)(B^A), A) + _S[2 * i + 1];
    }
    ct[0] = A;
    ct[1] = B;
    return ct;
}
WORD* cipher_rc5_Decrypt(const WORD * ct)//must input WORD x[2] for in and out
{
    WORD pt[2];
    int i;
    WORD B = ct[1], A = ct[0];
    for (i = _round; i > 0; i--)
    {
        B = CyclicRightShift(B - _S[2 * i + 1], A) ^ A;
        A = CyclicRightShift(A - _S[2 * i], B) ^ B;
    }
    pt[1] = B - _S[1];
    pt[0] = A - _S[0];
    return pt;
}





extern "C"
JNIEXPORT void JNICALL
Java_com_trial_bluetoothtrials_DeviceDetailActivityKt_rc5Setup(JNIEnv *env,jclass clazz,  jbyteArray entry) {
    unsigned char * input= reinterpret_cast<unsigned char *>(env->GetByteArrayElements(entry,
                                                                                       NULL));
    cipher_rc5_setup(input);

}extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_trial_bluetoothtrials_DeviceDetailActivityKt_rc5Decrypt(JNIEnv *env, jclass clazz, jbyteArray entry) {
    unsigned char decryptedValue[4];
    unsigned char * input= reinterpret_cast<unsigned char *>(env->GetByteArrayElements(entry,
                                                                                       NULL));
    WORD * decValuept=cipher_rc5_Decrypt(reinterpret_cast<const WORD *>(input));
    memcpy(decryptedValue,(unsigned char*)decValuept,sizeof(decryptedValue));
    jbyteArray ret = env->NewByteArray(4);
    env->SetByteArrayRegion(ret, 0, 4, reinterpret_cast<const jbyte *>(decryptedValue));
    return ret;
}extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_trial_bluetoothtrials_DeviceDetailActivityKt_rc5Encrypt(JNIEnv *env, jclass clazz, jbyteArray entry) {
    unsigned char encryptedValue[4];
    unsigned char * input= reinterpret_cast<unsigned char *>(env->GetByteArrayElements(entry,
                                                                                       NULL));
    WORD * encValuept=cipher_rc5_encrypt(reinterpret_cast<const WORD *>(input));
    memcpy(encryptedValue,(unsigned char*)encValuept,sizeof(encryptedValue));
    jbyteArray ret = env->NewByteArray(4);
    env->SetByteArrayRegion(ret, 0, 4, reinterpret_cast<const jbyte *>(encryptedValue));
    return ret;
}

