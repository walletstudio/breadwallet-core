//
//  BRRippleTransaction.h
//  Core
//
//  Created by Carl Cherry on 4/16/19.
//  Copyright © 2019 Breadwinner AG. All rights reserved.
//
//  See the LICENSE file at the project root for license information.
//  See the CONTRIBUTORS file at the project root for a list of contributors.
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "BRRipple.h"
#include "BRRippleBase.h"
#include "BRRipplePrivateStructs.h"
#include "BRRippleSerialize.h"
#include "BRRippleSignature.h"
#include "BRRippleAccount.h"
#include "BRCrypto.h"
#include "BRArray.h"
#include "BRInt.h"

typedef struct _txPaymentRecord {
    // The address to whom the payment is being sent
    BRRippleAddress targetAddress;

    // The payment amount (currently only supporting XRP drops
    BRRippleAmount amount;

    // (Optional) Arbitrary tag that identifies the reason for the payment
    // to the destination, or a hosted recipient to pay.
    BRRippleDestinationTag destinationTag;

    // (Optional) Arbitrary 256-bit hash representing a specific
    // reason or identifier for this payment.
    uint8_t invoiceId[32];

    // (Optional) Highest amount of source currency this transaction is
    // allowed to cost, including transfer fees, exchange rates, and slippage.
    // Does not include the XRP destroyed as a cost for submitting the transaction.
    // For non-XRP amounts, the nested field names MUST be lower-case.
    // Must be supplied for cross-currency/cross-issue payments.
    // Must be omitted for XRP-to-XRP payments.
    BRRippleAmount sendMax;

    // (Optional) Minimum amount of destination currency this transaction should deliver.
    // Only valid if this is a partial payment.
    //For non-XRP amounts, the nested field names are lower-case.
    BRRippleAmount deliverMin;

} BRRipplePaymentTxRecord;

struct BRRippleTransactionRecord {
    
    // COMMON FIELDS

    // The address of the account "doing" the transaction
    BRRippleAddress sourceAddress;

    // The Transaction type
    BRRippleTransactionType transactionType;

    // The transaction fee in drops (always XRP if I read the docs correctly)
    BRRippleAmount fee;
    
    // The next valid sequence number for the initiating account
    BRRippleSequence sequence;

    BRRippleFlags flags;
    BRRippleLastLedgerSequence lastLedgerSequence;

    // The account
    BRKey publicKey;

    // The ripple payment information
    // TODO in the future if more transaction are supported this could
    // be changed to a union of the various types
    BRRipplePaymentTxRecord payment;

    BRRippleSerializedTransaction signedBytes;

    BRRippleSignatureRecord signature;

    // Other fields that might show up when deserializing

    // Hash value identifying another transaction. If provided, this transaction
    //is only valid if the sending account's previously-sent transaction matches the provided hash.
    BRRippleTransactionHash accountTxnID;

    // Arbitrary integer used to identify the reason for this payment,
    // or a sender on whose behalf this transaction is made. Conventionally,
    // a refund should specify the initial payment's SourceTag as the refund
    // payment's DestinationTag.
    BRRippleSourceTag sourceTag;

    BRRippleMemoNode * memos;
};

struct BRRippleSerializedTransactionRecord {
    uint32_t size;
    uint8_t  *buffer;
    uint8_t  txHash[32];
};

void rippleSerializedTransactionRecordFree(BRRippleSerializedTransaction * signedBytes)
{
    assert(signedBytes);
    assert(*signedBytes);
    if ((*signedBytes)->buffer) {
        free((*signedBytes)->buffer);
    }
    free(*signedBytes);
}

static BRRippleTransaction createTransactionObject()
{
    BRRippleTransaction transaction = calloc (1, sizeof (struct BRRippleTransactionRecord));
    assert(transaction);
    return transaction;
}

extern BRRippleTransaction
rippleTransactionCreate(BRRippleAddress sourceAddress,
                        BRRippleAddress targetAddress,
                        BRRippleUnitDrops amount, // For now assume XRP drops.
                        BRRippleUnitDrops fee)
{
    BRRippleTransaction transaction = createTransactionObject();

    // Common fields
    transaction->fee.currencyType = 0; // XRP
    transaction->fee.amount.u64Amount = fee;
    transaction->sourceAddress = sourceAddress;
    transaction->transactionType = RIPPLE_TX_TYPE_PAYMENT;
    transaction->flags = 0x80000000; // tfFullyCanonicalSig
    transaction->lastLedgerSequence = 0;

    // Payment information
    transaction->payment.targetAddress = targetAddress;
    transaction->payment.amount.currencyType = 0; // XRP
    transaction->payment.amount.amount.u64Amount = amount; // XRP only
    
    transaction->signedBytes = NULL;

    return transaction;
}


extern void rippleTransactionFree(BRRippleTransaction transaction)
{
    assert(transaction);

    if (transaction->signedBytes) {
        rippleSerializedTransactionRecordFree(&transaction->signedBytes);
        transaction->signedBytes = NULL;
    }
    if (transaction->memos) {
        memoListFree(transaction->memos);
    }
    free(transaction);
}

int setFieldInfo(BRRippleField *fields, BRRippleTransaction transaction,
                  uint8_t * signature, int sig_length)
{
    int index = 0;
    
    // Convert all the content to ripple fields
    fields[index].typeCode = 8;
    fields[index].fieldCode = 1;
    fields[index++].data.address = transaction->sourceAddress;

    fields[index].typeCode = 1;
    fields[index].fieldCode = 2;
    fields[index++].data.i16 = transaction->transactionType;

    fields[index].typeCode = 2;
    fields[index].fieldCode = 4;
    fields[index++].data.i32 = transaction->sequence;

    fields[index].typeCode = 6;
    fields[index].fieldCode = 8;
    fields[index++].data.i64 = transaction->fee.amount.u64Amount;
    
    // Payment info
    fields[index].typeCode = 8;
    fields[index].fieldCode = 3;
    fields[index++].data.address = transaction->payment.targetAddress;

    fields[index].typeCode = 6;
    fields[index].fieldCode = 1;
    fields[index++].data.i64 = transaction->payment.amount.amount.u64Amount; // XRP only

    // Public key info
    fields[index].typeCode = 7;
    fields[index].fieldCode = 3;
    fields[index++].data.publicKey = transaction->publicKey;

    fields[index].typeCode = 2;
    fields[index].fieldCode = 2;
    fields[index++].data.i32 = transaction->flags;

    if (signature) {
        fields[index].typeCode = 7;
        fields[index].fieldCode = 4;
        memcpy(&fields[index].data.signature.signature, signature, sig_length);
        fields[index++].data.signature.sig_length = sig_length;
    }
    
    if (transaction->lastLedgerSequence > 0) {
        fields[index].typeCode = 2;
        fields[index].fieldCode = 27;
        fields[index++].data.i32 = transaction->lastLedgerSequence;
    }

    return index;
}

/*
 * Serialize the transaction
 *
 * @return serializedTransaction  valid BRRippleSerializedTransaction handle OR
 *                                NULL if unable to serialize
 */
static BRRippleSerializedTransaction
rippleTransactionSerialize (BRRippleTransaction transaction,
                            uint8_t *signature, int sig_length)
{
    assert(transaction);
    assert(transaction->transactionType == RIPPLE_TX_TYPE_PAYMENT);
    BRRippleField fields[10];

    int num_fields = setFieldInfo(fields, transaction, signature, sig_length);

    BRRippleSerializedTransaction signedBytes = NULL;

    int size = rippleSerialize(fields, num_fields, 0, 0);
    if (size > 0) {
        // I guess we will be sending back something
        // TODO validate the serialized bytes
        signedBytes = calloc(1, sizeof(struct BRRippleSerializedTransactionRecord));
        signedBytes->size = size + 512; // Allocate an extra 512 bytes for safety
        signedBytes->buffer = calloc(1, signedBytes->size);
        signedBytes->size = rippleSerialize(fields, num_fields, signedBytes->buffer, signedBytes->size);
        if (0 == signedBytes->size) {
            // Something bad happened - free memory and set pointer to NULL
            // LOG somewhere ???
            rippleSerializedTransactionRecordFree(&signedBytes);
            signedBytes = NULL;
        }
    }
    return signedBytes;
}

static void createTransactionHash(BRRippleSerializedTransaction signedBytes)
{
    assert(signedBytes);
    uint8_t bytes_to_hash[signedBytes->size + 4];

    // Add the transaction prefix before hashing
    bytes_to_hash[0] = 'T';
    bytes_to_hash[1] = 'X';
    bytes_to_hash[2] = 'N';
    bytes_to_hash[3] = 0;

    // Copy the rest of the bytes into the buffer
    memcpy(&bytes_to_hash[4], signedBytes->buffer, signedBytes->size);

    // Do a sha512 hash and use the first 32 bytes
    uint8_t md64[64];
    BRSHA512(md64, bytes_to_hash, sizeof(bytes_to_hash));
    memcpy(signedBytes->txHash, md64, 32);
}

extern BRRippleSerializedTransaction
rippleTransactionSerializeAndSign(BRRippleTransaction transaction, BRKey * privateKey,
                                  BRKey *publicKey, uint32_t sequence, uint32_t lastLedgerSequence)
{
    // If this transaction was previously signed - delete that info
    if (transaction->signedBytes) {
        free(transaction->signedBytes);
        transaction->signedBytes = 0;
    }

    // Add in the provided parameters
    transaction->sequence = sequence;
    transaction->lastLedgerSequence = lastLedgerSequence;
    
    // Add the public key to the transaction
    transaction->publicKey = *publicKey;
    
    // Serialize the bytes
    BRRippleSerializedTransaction serializedBytes = rippleTransactionSerialize (transaction, 0, 0);
    
    // Sign the bytes and get signature
    BRRippleSignature sig = signBytes(privateKey, serializedBytes->buffer, serializedBytes->size);

    // Re-serialize with signature
    transaction->signedBytes = rippleTransactionSerialize(transaction, sig->signature, sig->sig_length);

    // If we got a valid result then generate a hash
    if (transaction->signedBytes) {
        // Create and store a transaction hash of the transaction - the hash is attached to the signed
        // bytes object and will get destroyed if a subsequent serialization is done.
        createTransactionHash(transaction->signedBytes);
    }

    // Return the pointer to the signed byte object (or perhaps NULL)
    return transaction->signedBytes;
}

extern uint32_t getSerializedSize(BRRippleSerializedTransaction s)
{
    return s->size;
}
extern uint8_t* getSerializedBytes(BRRippleSerializedTransaction s)
{
    return s->buffer;
}

extern BRRippleTransactionHash rippleTransactionGetHash(BRRippleTransaction transaction)
{
    BRRippleTransactionHash hash;
    memset(hash.bytes, 0x00, sizeof(hash.bytes));

    // See if we have any signed bytes
    if (transaction->signedBytes) {
        memcpy(hash.bytes, transaction->signedBytes->txHash, 32);
    }

    return hash;
}

extern BRRippleTransactionHash rippleTransactionGetAccountTxnId(BRRippleTransaction transaction)
{
    assert(transaction);

    BRRippleTransactionHash hash;
    memset(hash.bytes, 0x00, sizeof(hash.bytes));

    // Copy whatever is in the field - might be nulls
    memcpy(hash.bytes, transaction->accountTxnID.bytes, 32);

    return hash;
}

extern BRRippleTransactionType rippleTransactionGetType(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->transactionType;
}

extern BRRippleUnitDrops rippleTransactionGetFee(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->fee.amount.u64Amount; // XRP always
}
extern BRRippleUnitDrops rippleTransactionGetAmount(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->payment.amount.amount.u64Amount; // XRP only
}
extern BRRippleSequence rippleTransactionGetSequence(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->sequence;
}
extern BRRippleFlags rippleTransactionGetFlags(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->flags;
}
extern BRRippleAddress rippleTransactionGetSource(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->sourceAddress;
}
extern BRRippleAddress rippleTransactionGetTarget(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->payment.targetAddress;
}

extern BRKey rippleTransactionGetPublicKey(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->publicKey;
}

extern BRRippleSignatureRecord rippleTransactionGetSignature(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->signature;
}

extern UInt256 rippleTransactionGetInvoiceID(BRRippleTransaction transaction)
{
    assert(transaction);
    UInt256 bytes;
    memset(bytes.u8, 0x00, sizeof(bytes.u8));
    memcpy(bytes.u8, transaction->payment.invoiceId, 32);
    return bytes;
}

extern BRRippleSourceTag rippleTransactionGetSourceTag(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->sourceTag;
}

extern BRRippleDestinationTag rippleTransactionGetDestinationTag(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->payment.destinationTag;
}

extern BRRippleLastLedgerSequence rippleTransactionGetLastLedgerSequence(BRRippleTransaction transaction)
{
    assert(transaction);
    return transaction->lastLedgerSequence;
}

extern BRRippleAmount rippleTransactionGetAmountRaw(BRRippleTransaction transaction,
                                                    BRRippleAmountType amountType)
{
    switch(amountType) {
        case RIPPLE_AMOUNT_TYPE_AMOUNT:
            return transaction->payment.amount;
        case RIPPLE_AMOUNT_TYPE_SENDMAX:
            return transaction->payment.sendMax;
        case RIPPLE_AMOUNT_TYPE_DELIVERMIN:
            return transaction->payment.deliverMin;
        case RIPPLE_AMOUNT_TYPE_FEE:
            return transaction->fee;
        default:
        {
            // Invalid type - return an invalid amount object
            BRRippleAmount amount;
            amount.currencyType = -1;
            return amount;
            break;
        }
    }
}

BRRippleTransactionType mapTransactionType(uint16_t txType)
{
    if (txType == 0) {
        return RIPPLE_TX_TYPE_PAYMENT;
    }
    return RIPPLE_TX_TYPE_UNKNOWN;
}

void getFieldInfo(BRRippleField *fields, int fieldLength, BRRippleTransaction transaction)
{
    for (int i = 0; i < fieldLength; i++) {
        switch(fields[i].typeCode) {
            case 1:
                if (2 == fields[i].fieldCode) {
                    // Map to our enum
                    transaction->transactionType = mapTransactionType(fields[i].data.i16);
                }
                break;
            case 2:
                if (2 == fields[i].fieldCode) {
                    transaction->flags = fields[i].data.i32;
                } else if (3 == fields[i].fieldCode) {
                    transaction->sourceTag = fields[i].data.i32;
                } else if (4 == fields[i].fieldCode) {
                    transaction->sequence = fields[i].data.i32;
                } else if (14 == fields[i].fieldCode) {
                    transaction->payment.destinationTag = fields[i].data.i32;
                } else if (27 == fields[i].fieldCode) {
                    transaction->lastLedgerSequence = fields[i].data.i32;
                }
                break;
            case 5: // Hash256
                if (9 == fields[i].fieldCode) {
                    memcpy(transaction->accountTxnID.bytes, fields[i].data.hash, 32);
                } else if (17 == fields[i].fieldCode) {
                    memcpy(transaction->payment.invoiceId, fields[i].data.hash, 32);
                }
                break;
            case 6: // Amount object
                if (8 == fields[i].fieldCode) { // fee)
                    transaction->fee = fields[i].data.amount;
                } else if (1 == fields[i].fieldCode) { // amount
                    transaction->payment.amount = fields[i].data.amount;
                } else if (9 == fields[i].fieldCode) { // fee
                    transaction->payment.sendMax = fields[i].data.amount;
                } else if (10 == fields[i].fieldCode) {
                    transaction->payment.deliverMin = fields[i].data.amount;
                }
            case 7: // Blob data
                if (3 == fields[i].fieldCode) { // public key
                    transaction->publicKey = fields[i].data.publicKey;
                } else if (4 == fields[i].fieldCode) { // signature
                    transaction->signature = fields[i].data.signature;
                }
                break;
            case 8: // Addresses - 20 bytes
                if (1 == fields[i].fieldCode) { // source address
                    transaction->sourceAddress = fields[i].data.address;
                } else if (3 == fields[i].fieldCode) { // target address
                    transaction->payment.targetAddress = fields[i].data.address;
                }
            default:
                break;
        }
    }
}

extern BRRippleTransaction
rippleTransactionCreateFromBytes(uint8_t *bytes, int length)
{
    BRRippleField * fields;
    array_new(fields, 10);
    rippleDeserialize(bytes, length, fields);
    
    BRRippleTransaction transaction = createTransactionObject();
    
    getFieldInfo(fields, array_count(fields), transaction);

    // Before we get rid of the fields - see if there are any fields that
    // need to be cleaned up (i.e. they allocated some memory
    size_t arraySize = array_count(fields);
    for(size_t i = 0; i < arraySize; i++) {
        if (15 == fields[i].typeCode && 9 == fields[i].fieldCode) {
            // An array of Memos
            transaction->memos = fields[i].memos;
        }
    }
    array_free(fields);

    return transaction;
}