/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file BlockVerifier.cpp
 *  @author mingzhenliu
 *  @date 20180921
 */
#include "BlockVerifier.h"
#include "ExecutiveContext.h"
#include <libethcore/Exceptions.h>
#include <libethcore/PrecompiledContract.h>
#include <libethcore/TransactionReceipt.h>
#include <libexecutive/ExecutionResult.h>
#include <libexecutive/Executive.h>
#include <exception>
using namespace dev;
using namespace std;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::executive;

ExecutiveContext::Ptr BlockVerifier::executeBlock(Block& block, BlockInfo const& parentBlockInfo)
{
    BLOCKVERIFIER_LOG(INFO) << LOG_DESC("[#executeBlock]Executing block")
                            << LOG_KV("txNum", block.transactions().size())
                            << LOG_KV("num", block.blockHeader().number())
                            << LOG_KV("hash", block.header().hash().abridged())
                            << LOG_KV("height", block.header().number())
                            << LOG_KV("receiptRoot", block.header().receiptsRoot())
                            << LOG_KV("stateRoot", block.header().stateRoot())
                            << LOG_KV("dbHash", block.header().dbHash())
                            << LOG_KV("parentHash", parentBlockInfo.hash.abridged())
                            << LOG_KV("parentNum", parentBlockInfo.number)
                            << LOG_KV("parentStateRoot", parentBlockInfo.stateRoot);

    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            parentBlockInfo, parentBlockInfo.stateRoot, executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR) << LOG_DESC("[#executeBlock] Error during initExecutiveContext")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt()
                              << errinfo_comment("Error during initExecutiveContext"));
    }

    BlockHeader tmpHeader = block.blockHeader();
    block.clearAllReceipts();
    for (Transaction const& tr : block.transactions())
    {
        EnvInfo envInfo(block.blockHeader(), m_pNumberHash,
            block.getTransactionReceipts().size() > 0 ?
                block.getTransactionReceipts().back().gasUsed() :
                0);
        envInfo.setPrecompiledEngine(executiveContext);
        std::pair<ExecutionResult, TransactionReceipt> resultReceipt =
            execute(envInfo, tr, OnOpFunc(), executiveContext);
        block.appendTransactionReceipt(resultReceipt.second);
        executiveContext->getState()->commit();
    }
    block.calReceiptRoot();
    block.header().setStateRoot(executiveContext->getState()->rootHash());
    block.header().setDBhash(executiveContext->getMemoryTableFactory()->hash());
    /// if executeBlock is called by consensus module, no need to compare receiptRoot and stateRoot
    /// since origin value is empty if executeBlock is called by sync module, need to compare
    /// receiptRoot, stateRoot and dbHash
    if (tmpHeader.receiptsRoot() != h256() && tmpHeader.stateRoot() != h256())
    {
        if (tmpHeader != block.blockHeader())
        {
            BLOCKVERIFIER_LOG(ERROR)
                << "Invalid Block with bad stateRoot or receiptRoot or dbHash"
                << LOG_KV("originHash", tmpHeader.hash().abridged())
                << LOG_KV("curHash", block.header().hash().abridged())
                << LOG_KV("orgReceipt", tmpHeader.receiptsRoot().abridged())
                << LOG_KV("curRecepit", block.header().receiptsRoot().abridged())
                << LOG_KV("orgState", tmpHeader.stateRoot().abridged())
                << LOG_KV("curState", block.header().stateRoot().abridged())
                << LOG_KV("orgDBHash", tmpHeader.dbHash().abridged())
                << LOG_KV("curDBHash", block.header().dbHash().abridged());
            BOOST_THROW_EXCEPTION(InvalidBlockWithBadStateOrReceipt() << errinfo_comment(
                                      "Invalid Block with bad stateRoot or ReceiptRoot"));
        }
    }
    return executiveContext;
}

std::pair<ExecutionResult, TransactionReceipt> BlockVerifier::executeTransaction(
    const BlockHeader& blockHeader, dev::eth::Transaction const& _t)
{
    ExecutiveContext::Ptr executiveContext = std::make_shared<ExecutiveContext>();
    BlockInfo blockInfo{blockHeader.hash(), blockHeader.number(), blockHeader.stateRoot()};
    try
    {
        m_executiveContextFactory->initExecutiveContext(
            blockInfo, blockHeader.stateRoot(), executiveContext);
    }
    catch (exception& e)
    {
        BLOCKVERIFIER_LOG(ERROR)
            << LOG_DESC("[#executeTransaction] Error during execute initExecutiveContext")
            << LOG_KV("errorMsg", boost::diagnostic_information(e));
    }

    EnvInfo envInfo(blockHeader, m_pNumberHash, 0);
    envInfo.setPrecompiledEngine(executiveContext);
    return execute(envInfo, _t, OnOpFunc(), executiveContext);
}

std::pair<ExecutionResult, TransactionReceipt> BlockVerifier::execute(EnvInfo const& _envInfo,
    Transaction const& _t, OnOpFunc const& _onOp, ExecutiveContext::Ptr executiveContext)
{
    auto onOp = _onOp;
#if ETH_VMTRACE
    if (isChannelVisible<VMTraceChannel>())
        onOp = Executive::simpleTrace();  // override tracer
#endif

    // Create and initialize the executive. This will throw fairly cheaply and quickly if the
    // transaction is bad in any way.
    Executive e(executiveContext->getState(), _envInfo);
    ExecutionResult res;
    e.setResultRecipient(res);
    e.initialize(_t);

    // OK - transaction looks valid - execute.
    u256 startGasUsed = _envInfo.gasUsed();
    if (!e.execute())
        e.go(onOp);
    e.finalize();

    /// mptstate calculates every transactions
    /// storagestate ignore hash calculation
    return make_pair(res, TransactionReceipt(executiveContext->getState()->rootHash(false),
                              startGasUsed + e.gasUsed(), e.logs(), e.status(),
                              e.takeOutput().takeBytes(), e.newAddress()));
}
