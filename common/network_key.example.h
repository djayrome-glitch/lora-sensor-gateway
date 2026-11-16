#pragma once
#include <stdint.h>

// MODÈLE — copie ce fichier en "network_key.h" (même dossier) et remplace les
// 16 octets par des valeurs aléatoires. network_key.h est ignoré par git.
//
// Générer une clé :
//   Linux / macOS : python3 -c "import os;print(', '.join('0x%02X'%b for b in os.urandom(16)))"
//   Windows (PowerShell) :
//     $b=New-Object byte[] 16;[Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b);($b|%{'0x{0:X2}' -f $_}) -join ', '
//
// La même clé doit être flashée sur la passerelle ET sur tous les nœuds.
static const uint8_t NETWORK_KEY[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
