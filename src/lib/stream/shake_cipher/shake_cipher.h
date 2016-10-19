/*
* SHAKE-128 as a stream cipher
* (C) 2016 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#ifndef BOTAN_SHAKE128_CIPHER_H__
#define BOTAN_SHAKE128_CIPHER_H__

#include <botan/stream_cipher.h>
#include <botan/secmem.h>

namespace Botan {

/**
* SHAKE-128 XOF presented as a stream cipher
*/
class BOTAN_DLL SHAKE_128 final : public StreamCipher
   {
   public:
      SHAKE_128();

      /**
      * Produce more XOF output
      */
      void cipher(const byte in[], byte out[], size_t length) override;

      /**
      * Seeking is not supported, this function will throw
      */
      void seek(u64bit offset) override;

      /**
      * IV not supported, this function will throw unless iv_len == 0
      */
      void set_iv(const byte iv[], size_t iv_len) override;

      bool valid_iv_length(size_t iv_len) const override { return (iv_len == 0); }

      /**
      * In principle SHAKE can accept arbitrary length inputs, but this
      * does not seem required for a stream cipher.
      */
      Key_Length_Specification key_spec() const override
         {
         return Key_Length_Specification(16, 160, 8);
         }

      void clear() override;
      std::string name() const override { return "SHAKE-128"; }
      StreamCipher* clone() const override { return new SHAKE_128; }

   private:
      void key_schedule(const byte key[], size_t key_len) override;

      secure_vector<uint64_t> m_state; // internal state
      secure_vector<byte> m_buffer; // ciphertext buffer
      size_t m_buf_pos; // position in m_buffer
   };

}

#endif
