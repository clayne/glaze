// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "glaze/beve/header.hpp"
#include "glaze/core/opts.hpp"
#include "glaze/core/read.hpp"
#include "glaze/file/file_ops.hpp"
#include "glaze/util/dump.hpp"

namespace glz
{
   template <>
   struct skip_value<BEVE>
   {
      template <auto Opts>
      inline static void op(is_context auto&& ctx, auto&& it, auto&& end) noexcept;
   };

   inline void skip_string_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      ++it;
      const auto n = int_from_compressed(ctx, it, end);
      if (bool(ctx.error)) [[unlikely]] {
         return;
      }
      if (uint64_t(end - it) < n) [[unlikely]] {
         ctx.error = error_code::unexpected_end;
         return;
      }
      it += n;
   }

   GLZ_ALWAYS_INLINE void skip_number_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      const auto tag = uint8_t(*it);
      const uint8_t byte_count = byte_count_lookup[tag >> 5];
      ++it;
      if ((it + byte_count) > end) [[unlikely]] {
         ctx.error = error_code::unexpected_end;
         return;
      }
      it += byte_count;
   }

   template <auto Opts>
   inline void skip_object_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      if (invalid_end(ctx, it, end)) {
         return;
      }
      const auto tag = uint8_t(*it);
      ++it;

      const auto n_keys = int_from_compressed(ctx, it, end);
      if (bool(ctx.error)) [[unlikely]] {
         return;
      }

      if ((tag & 0b00000'111) == tag::string) {
         for (size_t i = 0; i < n_keys; ++i) {
            const auto string_length = int_from_compressed(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            if (uint64_t(end - it) < string_length) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            it += string_length;

            skip_value<BEVE>::op<Opts>(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]]
               return;
         }
      }
      else if ((tag & 0b00000'111) == tag::number) {
         const uint8_t byte_count = byte_count_lookup[tag >> 5];
         for (size_t i = 0; i < n_keys; ++i) {
            const auto n = int_from_compressed(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            if (uint64_t(end - it) < byte_count * n) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            it += byte_count * n;

            skip_value<BEVE>::op<Opts>(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]]
               return;
         }
      }
      else {
         ctx.error = error_code::syntax_error;
         return;
      }
   }

   template <auto Opts>
   inline void skip_typed_array_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      const auto tag = uint8_t(*it);
      const uint8_t type = (tag & 0b000'11'000) >> 3;
      switch (type) {
      case 0: // floating point (fallthrough)
      case 1: // signed integer (fallthrough)
      case 2: { // unsigned integer
         ++it;
         const auto n = int_from_compressed(ctx, it, end);
         if (bool(ctx.error)) [[unlikely]] {
            return;
         }
         const uint8_t byte_count = byte_count_lookup[tag >> 5];
         if (uint64_t(end - it) < byte_count * n) [[unlikely]] {
            ctx.error = error_code::unexpected_end;
            return;
         }
         it += byte_count * n;
         break;
      }
      case 3: { // bool or string
         const bool is_bool = (tag & 0b00'1'00'000) >> 5;
         ++it;
         if (is_bool) {
            const auto n = int_from_compressed(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }

            const auto num_bytes = (n + 7) / 8;
            if (uint64_t(end - it) < num_bytes) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }
            it += num_bytes;
         }
         else {
            const auto n = int_from_compressed(ctx, it, end);
            if (bool(ctx.error)) [[unlikely]] {
               return;
            }
            if (uint64_t(end - it) < n) [[unlikely]] {
               ctx.error = error_code::unexpected_end;
               return;
            }

            it += n;
         }
         break;
      }
      default:
         ctx.error = error_code::syntax_error;
      }
   }

   template <auto Opts>
   inline void skip_untyped_array_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      ++it;
      const auto n = int_from_compressed(ctx, it, end);
      if (bool(ctx.error)) [[unlikely]] {
         return;
      }

      for (size_t i = 0; i < n; ++i) {
         skip_value<BEVE>::op<Opts>(ctx, it, end);
      }
   }

   template <auto Opts>
      requires(Opts.format == BEVE)
   void skip_array(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      switch (uint8_t(*it) & 0b00000'111) {
      case tag::typed_array: {
         skip_typed_array_beve<Opts>(ctx, it, end);
         break;
      }
      case tag::generic_array: {
         skip_untyped_array_beve<Opts>(ctx, it, end);
         break;
      }
      default:
         ctx.error = error_code::syntax_error;
      }
   }

   template <auto Opts>
   GLZ_ALWAYS_INLINE void skip_additional_beve(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      ++it;
      skip_value<BEVE>::op<Opts>(ctx, it, end);
   }

   template <auto Opts>
   inline void skip_value<BEVE>::op(is_context auto&& ctx, auto&& it, auto&& end) noexcept
   {
      using namespace glz::detail;

      if (invalid_end(ctx, it, end)) {
         return;
      }
      switch (uint8_t(*it) & 0b00000'111) {
      case tag::null: {
         ++it;
         break;
      }
      case tag::number: {
         skip_number_beve(ctx, it, end);
         break;
      }
      case tag::string: {
         skip_string_beve(ctx, it, end);
         break;
      }
      case tag::object: {
         skip_object_beve<Opts>(ctx, it, end);
         break;
      }
      case tag::typed_array: {
         skip_typed_array_beve<Opts>(ctx, it, end);
         break;
      }
      case tag::generic_array: {
         skip_untyped_array_beve<Opts>(ctx, it, end);
         break;
      }
      case tag::extensions: {
         skip_additional_beve<Opts>(ctx, it, end);
         break;
      }
      default:
         ctx.error = error_code::syntax_error;
      }
   }
}
