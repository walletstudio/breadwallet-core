/*
 * Created by Michael Carrara <michael.carrara@breadwallet.com> on 5/31/18.
 * Copyright (c) 2018 Breadwinner AG.  All right reserved.
 *
 * See the LICENSE file at the project root for license information.
 * See the CONTRIBUTORS file at the project root for a list of contributors.
 */
package com.breadwallet.corecrypto;

import com.breadwallet.corenative.crypto.BRCryptoBlockChainType;
import com.breadwallet.corenative.crypto.CoreBRCryptoCurrency;
import com.breadwallet.corenative.crypto.CoreBRCryptoNetwork;
import com.breadwallet.crypto.WalletManagerMode;
import com.google.common.base.Optional;
import com.google.common.base.Supplier;
import com.google.common.base.Suppliers;
import com.google.common.primitives.UnsignedLong;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/* package */
final class Network implements com.breadwallet.crypto.Network {

    /* package */
    static Network create(String uids, String name, boolean isMainnet, Currency currency, UnsignedLong height,
                          Map<Currency, NetworkAssociation> associations) {
        CoreBRCryptoNetwork core = null;

        String code = currency.getCode();
        if (code.equals(com.breadwallet.crypto.Currency.CODE_AS_BTC)) {
            core = CoreBRCryptoNetwork.createAsBtc(uids, name, isMainnet);

        } else if (code.equals(com.breadwallet.crypto.Currency.CODE_AS_BCH)) {
            core = CoreBRCryptoNetwork.createAsBch(uids, name, isMainnet);

        } else if (code.equals(com.breadwallet.crypto.Currency.CODE_AS_ETH)) {
            Optional<CoreBRCryptoNetwork> optional = CoreBRCryptoNetwork.createAsEth(uids, name, isMainnet);
            if (optional.isPresent()) {
                core = optional.get();
            } else {
                throw new IllegalArgumentException("Unsupported ETH network");
            }

        } else {
            core = CoreBRCryptoNetwork.createAsGen(uids, name);
        }

        core.setHeight(height);
        core.setCurrency(currency.getCoreBRCryptoCurrency());
        return new Network(core, associations);
    }

    /* package */
    static Network from(com.breadwallet.crypto.Network network) {
        if (network instanceof Network) {
            return (Network) network;
        }
        throw new IllegalArgumentException("Unsupported network instance");
    }

    private final CoreBRCryptoNetwork core;

    private final Supplier<Integer> typeSupplier;
    private final Supplier<String> uidsSupplier;
    private final Supplier<String> nameSupplier;
    private final Supplier<Boolean> isMainnetSupplier;
    private final Supplier<Currency> currencySupplier;
    private final Supplier<List<Currency>> currenciesSupplier;

    private Network(CoreBRCryptoNetwork core, Map<Currency, NetworkAssociation> associations) {
        this.core = core;

        for (Map.Entry<Currency, NetworkAssociation> entry: associations.entrySet()) {
            Currency currency = entry.getKey();
            NetworkAssociation association = entry.getValue();

            core.addCurrency(currency.getCoreBRCryptoCurrency(),
                    association.getBaseUnit().getCoreBRCryptoUnit(),
                    association.getDefaultUnit().getCoreBRCryptoUnit());

            for (Unit unit: association.getUnits()) {
                core.addCurrencyUnit(currency.getCoreBRCryptoCurrency(), unit.getCoreBRCryptoUnit());
            }
        }

        typeSupplier = Suppliers.memoize(core::getType);
        uidsSupplier = Suppliers.memoize(core::getUids);
        nameSupplier = Suppliers.memoize(core::getName);
        isMainnetSupplier = Suppliers.memoize(core::isMainnet);
        currencySupplier = Suppliers.memoize(() -> Currency.create(core.getCurrency()));
        currenciesSupplier = Suppliers.memoize(() -> {
            List<Currency> transfers = new ArrayList<>();

            UnsignedLong count = core.getCurrencyCount();
            for (UnsignedLong i = UnsignedLong.ZERO; i.compareTo(count) < 0; i = i.plus(UnsignedLong.ONE)) {
                transfers.add(Currency.create(core.getCurrency(i)));
            }

            return transfers;
        });
    }

    @Override
    public String getUids() {
        return uidsSupplier.get();
    }

    @Override
    public String getName() {
        return nameSupplier.get();
    }

    @Override
    public boolean isMainnet() {
        return isMainnetSupplier.get();
    }

    @Override
    public UnsignedLong getHeight() {
        return core.getHeight();
    }

    @Override
    public Currency getCurrency() {
        return currencySupplier.get();
    }

    @Override
    public Set<Currency> getCurrencies() {
        return new HashSet<>(currenciesSupplier.get());
    }

    @Override
    public Optional<Currency> getCurrencyByCode(String code) {
        for (Currency currency: getCurrencies()) {
            if (code.equals(currency.getCode())) {
                return Optional.of(currency);
            }
        }
        return Optional.absent();
    }

    @Override
    public boolean hasCurrency(com.breadwallet.crypto.Currency currency) {
        return core.hasCurrency(Currency.from(currency).getCoreBRCryptoCurrency());
    }

    @Override
    public Optional<Unit> baseUnitFor(com.breadwallet.crypto.Currency currency) {
        if (!hasCurrency(currency)) {
            return Optional.absent();
        }
        return core.getUnitAsBase(Currency.from(currency).getCoreBRCryptoCurrency()).transform(Unit::create);
    }

    @Override
    public Optional<Unit> defaultUnitFor(com.breadwallet.crypto.Currency currency) {
        if (!hasCurrency(currency)) {
            return Optional.absent();
        }
        return core.getUnitAsDefault(Currency.from(currency).getCoreBRCryptoCurrency()).transform(Unit::create);
    }

    @Override
    public Optional<Set<? extends com.breadwallet.crypto.Unit>> unitsFor(com.breadwallet.crypto.Currency currency) {
        if (!hasCurrency(currency)) {
            return Optional.absent();
        }

        Set<Unit> units = new HashSet<>();

        CoreBRCryptoCurrency currencyCore = Currency.from(currency).getCoreBRCryptoCurrency();
        UnsignedLong count = core.getUnitCount(currencyCore);

        for (UnsignedLong i = UnsignedLong.ZERO; i.compareTo(count) < 0; i = i.plus(UnsignedLong.ONE)) {
            Optional<Unit> unit = core.getUnitAt(currencyCore, i).transform(Unit::create);
            if (!unit.isPresent()) {
                return Optional.absent();
            }

            units.add(unit.get());
        }

        return Optional.of(units);
    }

    @Override
    public Optional<Boolean> hasUnitFor(com.breadwallet.crypto.Currency currency, com.breadwallet.crypto.Unit unit) {
        return unitsFor(currency).transform(input -> input.contains(unit));
    }

    @Override
    public List<WalletManagerMode> getSupportedModes() {
        switch (typeSupplier.get()) {
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_BTC: {
                return Arrays.asList(WalletManagerMode.P2P_ONLY, WalletManagerMode.API_WITH_P2P_SUBMIT);
            }
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_ETH: {
                return Arrays.asList(WalletManagerMode.API_ONLY, WalletManagerMode.API_WITH_P2P_SUBMIT);
            }
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_GEN: {
                return Collections.singletonList(WalletManagerMode.API_ONLY);
            }
            default:
                throw new IllegalStateException("Invalid network type");
        }
    }

    @Override
    public Optional<Address> addressFor(String address) {
        switch (typeSupplier.get()) {
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_BTC: {
                return Address.createAsBtc(address);
            }
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_ETH: {
                return Address.createAsEth(address);
            }
            case BRCryptoBlockChainType.BLOCK_CHAIN_TYPE_GEN: {
                // TODO(fix): Implement this
                return Optional.absent();
            }
            default:
                throw new IllegalStateException("Invalid network type");
        }
    }

    @Override
    public String toString() {
        return getName();
    }

    @Override
    public boolean equals(Object object) {
        if (this == object) {
            return true;
        }

        if (!(object instanceof Network)) {
            return false;
        }

        Network network = (Network) object;
        return core.equals(network.core);
    }

    @Override
    public int hashCode() {
        return Objects.hash(core);
    }

    /* package */
    void setHeight(UnsignedLong height) {
        core.setHeight(height);
    }

    /* package */
    CoreBRCryptoNetwork getCoreBRCryptoNetwork() {
        return core;
    }
}
