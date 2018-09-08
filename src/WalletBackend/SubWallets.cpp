// Copyright (c) 2018, The TurtleCoin Developers
// 
// Please see the included LICENSE file for more information.

/////////////////////////////////////
#include <WalletBackend/SubWallets.h>
/////////////////////////////////////

/* TODO: Remove */
#include <Common/StringTools.h>

#include <config/CryptoNoteConfig.h>

#include <ctime>

/* TODO: Remove */
#include <iostream>

#include <random>

//////////////////////////
/* NON MEMBER FUNCTIONS */
//////////////////////////

namespace
{

    uint64_t getCurrentTimestampAdjusted()
    {
        /* Get the current time as a unix timestamp */
        std::time_t time = std::time(nullptr);

        /* Take the amount of time a block can potentially be in the past/future */
        std::initializer_list<uint64_t> limits =
        {
            CryptoNote::parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT,
            CryptoNote::parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT_V3,
            CryptoNote::parameters::CRYPTONOTE_BLOCK_FUTURE_TIME_LIMIT_V4
        };

        /* Get the largest adjustment possible */
        uint64_t adjust = std::max(limits);

        /* Take the earliest timestamp that will include all possible blocks */
        return time - adjust;
    }

    /* Converts a height to a timestamp */
    uint64_t scanHeightToTimestamp(const uint64_t scanHeight)
    {
        if (scanHeight == 0)
        {
            return 0;
        }

        /* Get the amount of seconds since the blockchain launched */
        uint64_t secondsSinceLaunch = scanHeight * 
                                      CryptoNote::parameters::DIFFICULTY_TARGET;

        /* Get the genesis block timestamp and add the time since launch */
        uint64_t timestamp = CryptoNote::parameters::GENESIS_BLOCK_TIMESTAMP
                           + secondsSinceLaunch;

        /* Don't make timestamp too large or daemon throws an error */
        if (timestamp >= getCurrentTimestampAdjusted())
        {
            return getCurrentTimestampAdjusted();
        }

        return timestamp;
    }
    
} // namespace

///////////////////////////////////
/* CONSTRUCTORS / DECONSTRUCTORS */
///////////////////////////////////

SubWallets::SubWallets()
{
}

/* Makes a new view only subwallet */
SubWallets::SubWallets(const Crypto::PublicKey publicSpendKey,
                       const std::string address,
                       const uint64_t scanHeight, const bool newWallet)
{
    uint64_t timestamp = newWallet ? getCurrentTimestampAdjusted() : 0;

    m_subWallets[publicSpendKey]
        = SubWallet(publicSpendKey, address, scanHeight, timestamp);

    m_publicSpendKeys.push_back(publicSpendKey);
}

/* Makes a new subwallet */
SubWallets::SubWallets(const Crypto::SecretKey privateSpendKey,
                       const std::string address,
                       const uint64_t scanHeight, const bool newWallet)
{
    Crypto::PublicKey publicSpendKey;

    Crypto::secret_key_to_public_key(privateSpendKey, publicSpendKey);

    uint64_t timestamp = newWallet ? getCurrentTimestampAdjusted() : 0;

    m_subWallets[publicSpendKey] = SubWallet(
        publicSpendKey, privateSpendKey, address, scanHeight, timestamp
    );

    m_publicSpendKeys.push_back(publicSpendKey);
}

/////////////////////
/* CLASS FUNCTIONS */
/////////////////////

/* So much duplicated code ;_; */
void SubWallets::addSubWallet(const Crypto::PublicKey publicSpendKey,
                              const std::string address,
                              const uint64_t scanHeight, const bool newWallet)
{
    uint64_t timestamp = newWallet ? getCurrentTimestampAdjusted() : 0;

    m_subWallets[publicSpendKey]
        = SubWallet(publicSpendKey, address, scanHeight, timestamp);

    m_publicSpendKeys.push_back(publicSpendKey);
}

void SubWallets::addSubWallet(const Crypto::SecretKey privateSpendKey,
                              const std::string address,
                              const uint64_t scanHeight, const bool newWallet)
{
    Crypto::PublicKey publicSpendKey;

    Crypto::secret_key_to_public_key(privateSpendKey, publicSpendKey);

    uint64_t timestamp = newWallet ? getCurrentTimestampAdjusted() : 0;

    m_subWallets[publicSpendKey] = SubWallet(
        publicSpendKey, privateSpendKey, address, scanHeight, timestamp
    );

    m_publicSpendKeys.push_back(publicSpendKey);
}

/* Gets the starting height, and timestamp to begin the sync from. Only one of
   these will be non zero, which will the the lowest one (ignoring null values).

   So, if for example, one subwallet has a start height of 400,000, and another
   has a timestamp of something corresponding to 300,000, we would return
   zero for the start height, and the timestamp corresponding to 300,000.

   Alternatively, if the timestamp corresponded to 500,000, we would return
   400,000 for the height, and zero for the timestamp. */
std::tuple<uint64_t, uint64_t> SubWallets::getMinInitialSyncStart()
{
    /* Get the smallest sub wallet (by timestamp) */
    auto minElementByTimestamp = *std::min_element(m_subWallets.begin(), m_subWallets.end(),
    [](const auto &lhs, const auto &rhs)
    {
        return lhs.second.m_syncStartTimestamp < rhs.second.m_syncStartTimestamp;
    });

    uint64_t minTimestamp = minElementByTimestamp.second.m_syncStartTimestamp;

    /* Get the smallest sub wallet (by height) */
    auto minElementByHeight = *std::min_element(m_subWallets.begin(), m_subWallets.end(),
    [](const auto &lhs, const auto &rhs)
    {
        return lhs.second.m_syncStartHeight < rhs.second.m_syncStartHeight;
    });

    uint64_t minHeight = minElementByHeight.second.m_syncStartHeight;

    /* One or both of the values are zero, caller will use whichever is non
       zero */
    if (minHeight == 0 || minTimestamp == 0)
    {
        return {minHeight, minTimestamp};
    }

    /* Convert timestamp to height so we can compare them, then return the min
       of the two, and set the other to zero */
    uint64_t timestampFromHeight = scanHeightToTimestamp(minHeight);

    if (timestampFromHeight < minTimestamp)
    {
        return {minHeight, 0};
    }
    else
    {
        return {0, minTimestamp};
    }
}

void SubWallets::addTransaction(Transaction tx)
{
    m_transactions.push_back(tx);

    /* We can regenerate the balance from the transactions, but this will be
       faster, as getting the balance is a common operation */
    for (const auto & [pubKey, amount] : tx.transfers)
    {
        m_subWallets[pubKey].m_balance += amount;
        
        if (amount != 0 && tx.fee == 0)
        {
            std::cout << "Coinbase transaction found!" << std::endl;
        }
        else if (amount > 0)
        {
            std::cout << "Incoming transaction found!" << std::endl;
        }
        else if (amount < 0)
        {
            std::cout << "Outgoing transaction found!" << std::endl;
        }
        else
        {
            std::cout << "Fusion transaction found!" << std::endl;
        }

        std::cout << "Hash: " << Common::podToHex(tx.hash) << std::endl
                  << "Amount: " << std::abs(amount) << std::endl
                  << "Fee: " << tx.fee << std::endl
                  << "Block height: " << tx.blockHeight << std::endl
                  << "Timestamp: " << tx.timestamp << std::endl
                  << "Payment ID: " << tx.paymentID << std::endl << std::endl;
    }
}

void SubWallets::generateAndStoreKeyImage(Crypto::PublicKey publicSpendKey,
                                          Crypto::KeyDerivation derivation,
                                          size_t outputIndex,
                                          uint64_t amount)
{
    const auto subWallet = m_subWallets.find(publicSpendKey);

    /* Check it exists */
    if (subWallet != m_subWallets.end())
    {
        subWallet->second.generateAndStoreKeyImage(
            derivation, outputIndex, amount
        );
    }
}

std::tuple<bool, Crypto::PublicKey>
    SubWallets::getKeyImageOwner(Crypto::KeyImage keyImage)
{
    for (const auto & [publicKey, subWallet] : m_subWallets)
    {
        /* See if the sub wallet contains the key image */
        auto it = std::find_if(subWallet.m_keyImages.begin(),
                               subWallet.m_keyImages.end(),
        [&keyImage](const WalletTypes::TransactionInput &input)
        {
            return input.keyImage == keyImage;
        });

        /* Found the key image */
        if (it != subWallet.m_keyImages.end())
        {
            return {true, subWallet.m_publicSpendKey};
        }
    }

    return {false, Crypto::PublicKey()};
}

/* Remember if the transaction suceeds, we need to remove these key images
   so we don't double spend.
   
   This may throw if you don't validate the user has enough balance, and
   that each of the subwallets exist. */
std::vector<WalletTypes::TransactionInput>
    SubWallets::getTransactionInputsForAmount(uint64_t amount,
        bool takeFromAll, std::vector<Crypto::PublicKey> subWalletsToTakeFrom)
{
    /* If we're able to take from every subwallet, set the wallets to take from
       to all our public spend keys */
    if (takeFromAll)
    {
        subWalletsToTakeFrom = m_publicSpendKeys;
    }

    std::vector<SubWallet> wallets;

    /* Loop through each public key and grab the associated wallet */
    for (const auto &publicKey : subWalletsToTakeFrom)
    {
        wallets.push_back(m_subWallets[publicKey]);
    }

    std::vector<WalletTypes::TransactionInput> availableInputs;

    /* Copy the key images from this sub wallet to inputs */
    for (const auto &subWallet : wallets)
    {
        std::copy(subWallet.m_keyImages.begin(), subWallet.m_keyImages.end(),
                  std::back_inserter(availableInputs));
    }

    /* Shuffle the inputs */
    std::shuffle(availableInputs.begin(), availableInputs.end(), std::random_device{});

    uint64_t foundMoney = 0;

    std::vector<WalletTypes::TransactionInput> inputsToUse;

    /* Loop through each input */
    for (const auto &input : availableInputs)
    {
        /* Add each input */
        inputsToUse.push_back(input);

        foundMoney += input.amount;

        /* Keep adding until we have enough money for the transaction */
        if (foundMoney >= amount)
        {
            return inputsToUse;
        }
    }

    /* Not enough money to cover the transaction */
    throw std::invalid_argument("Not enough funds found!");
}

/* Gets the address of the 'first' wallet. Since this is an unordered_map, the
   wallet this points to is undefined. You should only really use this in a
   single wallet container */
std::string SubWallets::getDefaultChangeAddress()
{
    return (*m_subWallets.begin()).second.m_address;
}
