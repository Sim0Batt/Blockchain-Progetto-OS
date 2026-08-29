#!/bin/bash

source utils/errors.sh


# Questa funzione verifica che il blocco seguente nella catena sia effettivamente quello giusto
verifyIndex(){
  local expectedValue=$1
  local actualValue=$2

  if [[ "$actualValue" -ne "$expectedValue" ]]; then
      echo "Error: expected value different from the current one, the next block is different from the expected" >&2
      return $INVALID_BLOCK
  fi

  return $SUCCESS
}

# Questa funzione verifica che la catena non sia "spezzata"
verifyChainLink(){
  local actualHashField=$1
  local actualPrevHash=$2

  if [[ "$actualPrevHash" != "$actualHashField" ]]; then
      echo "Error: prev hash different from the actual one, error in the chain" >&2
      return $INVALID_BLOCK
  fi

  return $SUCCESS
}

# Questa funzione verifica la validità dell'albero merkel
verifyMerkelTree(){
  local actualMerkelBlock=$1
  local calculatedMerkelBlock=$2

  if [[ "$actualMerkelBlock" != "$calculatedMerkelBlock" ]]; then
      echo "Error: the actual merkel block is different from the calculated one" >&2
      return $INVALID_BLOCK
  fi

  return $SUCCESS
}

# Questa funzione verifica lo stato del file CSV
readBlockChainCsv(){
  local filePath=$1

  # -f controlla che il file esista nel path
  if [[ ! -f "$filePath" ]]; then
    echo "Error: the CSV file does not exist" >&2
    return $PARSE_ERROR
  fi

  # -s verifica se il file è vuoto
  if [[ ! -s "$filePath" ]]; then
    echo "Error: the CSV file is empty" >&2
    return $PARSE_ERROR
  fi

  return $SUCCESS
}

# SHA
calculateSha256() {
  echo -n "$1" | sha256sum |  awk '{print $1}'
}


# Implementazione di Merkle
cmdMerkle(){
  local txString="$1"
  local emptyHash=$(calculateSha256 "")

  if [[ -z "$txString" ]]; then
    echo "$emptyHash"
    return $SUCCESS
  fi

  local -a txs
  IFS=':' read -ra parts <<< "${txString//::/:}"
  for part in "${parts[@]}"; do
    if [[ -n "$part" ]]; then
      txs+=("$part")
    fi
  done

  local -a hashes
  for tx in "${txs[@]}"; do
    hashes+=($(calculateSha256 "$tx"))
  done

  local count=${#hashes[@]}

  if [[ $count -gt 0 ]]; then
    local -a nextHashes
    while true; do
      if (( count % 2 != 0 )); then
        hashes+=("$emptyHash")
        ((count++))
      fi

      nextHashes=()
      for (( i=0; i<count; i+=2)); do
        local combined="${hashes[i]}${hashes[i+1]}"
        nextHashes+=($(calculateSha256 "$combined"))
      done

      hashes=("${nextHashes[@]}")
      count=${#hashes[@]}

      if [[ $count -le 1 ]]; then
        break
      fi
    done
  fi

  echo "${hashes[0]}"
}

cmdHash() {
  local blockData="$1"

  blockData=$(echo "$blockData" | tr -d ' "' | tr -d '\n' | tr -d '\r')

  local headerHex="${blockData:0:176}"
  calculateSha256 "$headerHex"
}

cmdVerify(){
  local filePath="$1"

  readBlockChainCsv "$filePath"
  if [[ $? -ne $SUCCESS ]]; then
    exit $PARSE_ERROR
  fi

  local expectedIndex=0
  local expectedHash=""
  local lineNum=0

  while IFS= read -r line || [[ -n "$line" ]]; do
    ((lineNum++))

    if [[ $lineNum -eq 1 ]]; then
      continue
    fi

    local cleanLine=$(echo "$line" | tr -d '\r' | tr -d '\n')
    if [[ -z "$cleanLine" ]]; then
      continue
    fi

    local idxHex=$(echo "$cleanLine" | tr -d ' ' | awk -F',' '{print $1}')
    local tsHex=$(echo "$cleanLine" | tr -d ' ' | awk -F',' '{print $2}')
    local prevHash=$(echo "$cleanLine" | tr -d ' ' | awk -F',' '{print $3}')
    local merkleCsv=$(echo "$cleanLine" | tr -d ' ' | awk -F',' '{print $4}')
    local nonceHex=$(echo "$cleanLine" | tr -d ' ' | awk -F',' '{print $5}')

    local txs_raw=$(echo "$cleanLine" | grep -o '".*"' | sed 's/"//g')
    if [[ -z "$txs_raw" ]]; then
      txs_raw=$(echo "$cleanLine" | awk -F',' '{print $6}')
    fi

    local idxDec=$((16#$idxHex))
    verifyIndex "$expectedIndex" "$idxDec"
    if [[ $? -ne $SUCCESS ]]; then exit $INVALID_BLOCK; fi

    if [[ $idxDec -gt 0 ]]; then
      verifyChainLink "$expectedHash" "$prevHash"
      if [[ $? -ne $SUCCESS ]]; then exit $CHAIN_MISMATCH; fi
    fi

    local calcMekle=$(cmdMerkle "$txs_raw")
    verifyMerkelTree "$merkleCsv" "$calcMekle"
    if [[ $? -ne $SUCCESS ]]; then exit $INVALID_BLOCK; fi

    local headerCombined="${idxHex}${tsHex}${prevHash}${merkleCsv}${nonceHex}"
    expectedHash=$(cmdHash "$headerCombined")

    ((expectedIndex++))


  done < "$filePath"

  echo "Verification completed, no errors"
  exit $SUCCESS
}


# Parsing degli argomenti
if [[ $# -lt 2 ]]; then
    echo "Usage: $0 --verify <state.csv> | --hash <block_data> | --merkle <transactions>"
    exit $PARSE_ERROR
fi

case "$1" in
    --verify)
        cmdVerify "$2"
        ;;
    --hash)
        cmdHash "$2"
        ;;
    --merkle)
        cmdMerkle "$2"
        ;;
    *)
        echo "Comando sconosciuto: $1"
        exit $PARSE_ERROR
        ;;
esac
