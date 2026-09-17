#include "PackageDeviceCrypto.h"

#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <cstring>

namespace RuntimePackages {

bool PackageDeviceTrustVerifier::operator()(uint32_t keyId, const uint8_t digest[32],
                                             const uint8_t signature[64]) const {
  if (!digest || !signature) return false;
  const TrustedPackageSigner* selected =
      selectTrustedPackageSigner(archive_, keyId, signers_, count_);
  if (!selected) return false;

  mbedtls_ecp_group group;
  mbedtls_ecp_point point;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&point);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool accepted =
      mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
      mbedtls_ecp_point_read_binary(&group, &point, selected->publicPoint, 65) == 0 &&
      mbedtls_ecp_check_pubkey(&group, &point) == 0 &&
      mbedtls_mpi_read_binary(&r, signature, 32) == 0 &&
      mbedtls_mpi_read_binary(&s, signature + 32, 32) == 0 &&
      mbedtls_ecdsa_verify(&group, digest, 32, &point, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&point);
  mbedtls_ecp_group_free(&group);
  return accepted;
}

} // namespace RuntimePackages
