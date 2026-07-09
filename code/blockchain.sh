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




