#ifdef WIN32
#include <ecrt_windows_py.h>//just for IDE vs2015, it does not really work
#endif
#ifdef UNIX
#include <ecrt.h>
#include <rtdk.h>
#include <native/task.h>
#include <native/timer.h>
#include <sys/mman.h>
// following for pipe
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <rtdm/rtdm.h>
#include <rtdm/rtipc.h>
#endif

#include <mutex>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <atomic>
#include <memory>
#include <typeinfo>
#include <thread>
#include <chrono>
#include <future>

#include "aris_control_ethercat.h"

namespace aris
{
	namespace control
	{

#ifdef WIN32
		auto aris_rt_set_periodic(int nanoseconds)->void
		{
		};
		auto aris_rt_wait_period()->void
		{
		};
		auto aris_rt_timer_read()->std::int64_t
		{
			return 0;
		}
		auto aris_rt_task_start(void(*task_func)(void*), void*param)->void *
		{
			return nullptr;
		}
		auto aris_rt_task_stop(void *handle)->void
		{
		}
#endif

#ifdef UNIX
		auto aris_rt_set_periodic(int nanoseconds)->void 
		{
			rt_task_set_periodic(NULL, TM_NOW, nanoseconds);
		};
		auto aris_rt_wait_period()->void 
		{
			rt_task_wait_period(NULL);
		};
		auto aris_rt_timer_read()->std::int64_t
		{
			return rt_timer_read();
		}
		auto aris_rt_task_start(void(*task_func)(void*), void*param)->void *
		{
			auto handle = std::make_unique<RT_TASK>();
			rt_print_auto_init(1);
			rt_task_create(handle.get(), "realtime core", 0, 99, T_FPU);
			rt_task_start(handle.get(), task_func, param);
			return handle.release();
		}
		auto aris_rt_task_stop(void *handle)
		{
			rt_task_delete(reinterpret_cast<RT_TASK*>(handle));
		}
#endif
		
		struct SlaveDomainInfo
		{
			ec_domain_t* domain_;
			char* domain_pd_;
		};
		auto aris_ecrt_master_init()->void {};
		auto aris_ecrt_master_sync()->void {};
		auto aris_ecrt_master_receive()->void {};
		auto aris_ecrt_master_send()->void {};
		auto aris_ecrt_master_start()->void {};
		auto aris_ecrt_master_stop()->void {};
		auto aris_ecrt_slave_config(void* master_handle, Slave *sla)->void *
		{
			auto ec_mst = static_cast<ec_master_t*>(master_handle);
			
			ec_domain_t* domain_;
			char* domain_pd_;
			
			std::vector<ec_pdo_entry_reg_t> ec_pdo_entry_reg_vec;
			std::vector<ec_pdo_info_t> ec_pdo_info_vec_tx, ec_pdo_info_vec_rx;
			std::list<std::vector<ec_pdo_entry_info_t>> ec_pdo_entry_info_vec_list;
			ec_sync_info_t ec_sync_info[5];
			ec_slave_config_t* ec_slave_config_;

			// create ecrt structs  //
			for (auto &pdo_group : sla->pdoGroupPool())
			{
				std::vector<ec_pdo_entry_info_t> ec_pdo_entry_info_vec;

				for (auto &pdo : pdo_group)
				{
					ec_pdo_entry_reg_vec.push_back(ec_pdo_entry_reg_t{ sla->alias(), static_cast<std::uint16_t>(sla->father().children().size()), sla->venderID(), sla->productCode(), pdo.index(), pdo.subindex(), &pdo.offset_ });
					ec_pdo_entry_info_vec.push_back(ec_pdo_entry_info_t{ pdo.index(), pdo.subindex(), pdo.dataSize() });
				}

				if (pdo_group.tx())
				{
					ec_pdo_info_vec_tx.push_back(ec_pdo_info_t{ pdo_group.index(), static_cast<std::uint8_t>(pdo_group.size()), ec_pdo_entry_info_vec.data() });
				}
				else
				{
					ec_pdo_info_vec_rx.push_back(ec_pdo_info_t{ pdo_group.index(), static_cast<std::uint8_t>(pdo_group.size()), ec_pdo_entry_info_vec.data() });
				}
			}
			ec_pdo_entry_reg_vec.push_back(ec_pdo_entry_reg_t{});

			ec_sync_info[0] = ec_sync_info_t{ 0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE };
			ec_sync_info[1] = ec_sync_info_t{ 1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE };
			ec_sync_info[2] = ec_sync_info_t{ 2, EC_DIR_OUTPUT, static_cast<unsigned int>(ec_pdo_info_vec_rx.size()), ec_pdo_info_vec_rx.data(), EC_WD_ENABLE };
			ec_sync_info[3] = ec_sync_info_t{ 3, EC_DIR_INPUT, static_cast<unsigned int>(ec_pdo_info_vec_tx.size()), ec_pdo_info_vec_tx.data(), EC_WD_ENABLE };
			ec_sync_info[4] = ec_sync_info_t{ 0xff };

			for (auto &reg : ec_pdo_entry_reg_vec)	reg.position = sla->position();

			// Create domain
			if (!(domain_ = ecrt_master_create_domain(ec_mst)))throw std::runtime_error("failed to create domain");

			// Get the slave configuration 
			if (!(ec_slave_config_ = ecrt_master_slave_config(ec_mst, sla->alias(), sla->position(), sla->venderID(), sla->productCode())))
			{
				throw std::runtime_error("failed to slave config");
			}

			// Config Sdo
			for (auto &sdo : sla->sdoPool())
			{
				if (!(sdo.option() & Sdo::CONFIG)) continue;

				switch (sdo.dataSize())
				{
				case 8:		ecrt_slave_config_sdo8(ec_slave_config_, sdo.index(), sdo.subindex(), sdo.configValueUint8()); break;
				case 16:	ecrt_slave_config_sdo16(ec_slave_config_, sdo.index(), sdo.subindex(), sdo.configValueUint16()); break;
				case 32:	ecrt_slave_config_sdo32(ec_slave_config_, sdo.index(), sdo.subindex(), sdo.configValueUint32()); break;
				default:    throw std::runtime_error("invalid size of sdo, it must be 8, 16 or 32");
				}
			}

			// Configure the slave's PDOs and sync masters
			if (ecrt_slave_config_pdos(ec_slave_config_, 4, ec_sync_info))throw std::runtime_error("failed to slave config pdos");

			// Configure the slave's domain
			if (ecrt_domain_reg_pdo_entry_list(domain_, ec_pdo_entry_reg_vec.data()))throw std::runtime_error("failed domain_reg_pdo_entry");

			// Configure the slave's discrete clock
			if (sla->distributedClock())ecrt_slave_config_dc(ec_slave_config_, sla->distributedClock(), 1000000, 4400000, 0, 0);

			return new SlaveDomainInfo{ domain_ , domain_pd_ };
		};
		auto aris_ecrt_slave_receive()->void 
		{

		}
		auto aris_ecrt_slave_send()->void {};
		auto aris_ecrt_slave_read_pdo()->void {};
		auto aris_ecrt_slave_write_pdo()->void {};
		auto aris_ecrt_slave_read_sdo()->void {};
		auto aris_ecrt_slave_write_sdo()->void {};
		
		struct Slave::Imp
		{
		public:
			auto read()->void
			{
#ifdef UNIX 
				ecrt_domain_process(domain_);
#endif
			}
			auto write()->void
			{
#ifdef UNIX
				ecrt_domain_queue(domain_);
#endif
			}
			auto init()->void;
			Imp(Slave*slave) :slave_(slave) {}


			void* ec_slave_handle_{ nullptr };


			ec_domain_t* domain_;
			std::uint8_t* domain_pd_;

			SlaveType *slave_type_;

			aris::core::ObjectPool<PdoGroup, Element> *pdo_group_pool_;
			aris::core::ObjectPool<Sdo, Element> *sdo_pool_;
			std::map<std::uint16_t, std::map<std::uint8_t, std::pair<int, int> > > pdo_map_;
			std::map<std::uint16_t, std::map<std::uint8_t, int>> sdo_map_;

			Slave *slave_;
			RxType rx_data_;
			TxType tx_data_;

			friend class Master::Imp;
			friend class Slave;
			friend class Master;
		};

		class Master::Imp
		{
		public:
			static auto rt_task_func(void *master)->void
			{
				auto &mst = *reinterpret_cast<Master*>(master);

				aris_rt_set_periodic(mst.imp_->sample_period_ns_);

				while (!mst.imp_->is_stopping_)
				{
					aris_rt_wait_period();

					mst.imp_->read();//motors and sensors get data

					/// tg begin
					mst.controlStrategy();
					/// tg end

					mst.imp_->sync(aris_rt_timer_read());
					mst.imp_->write();//motor data write and state machine/mode transition

					mst.dataLogger().logDataRT();//sent data to nrt
				}
			};
			auto read()->void
			{
#ifdef UNIX
				ecrt_master_receive(ec_master_);
#endif
				for (auto &sla : *slave_pool_)
				{
					sla.readUpdate();
					sla.imp_->read();
				}
			};
			auto write()->void
			{
				for (auto &sla : *slave_pool_)
				{
					sla.writeUpdate();
					sla.imp_->write();
				}
#ifdef UNIX
				ecrt_master_send(ec_master_);
#endif
			}
			auto sync(uint64_t ns)->void
			{
#ifdef UNIX
				ecrt_master_application_time(ec_master_, ns);
				ecrt_master_sync_reference_clock(ec_master_);
				ecrt_master_sync_slave_clocks(ec_master_);
#endif
			};

			aris::core::ObjectPool<SlaveType, Element> *slave_type_pool_;
			aris::core::ObjectPool<Slave, Element> *slave_pool_;
			aris::core::RefPool<Slave::TxType> tx_data_pool_;
			aris::core::RefPool<Slave::RxType> rx_data_pool_;

			//for log
			DataLogger* data_logger_;
			std::atomic_bool is_running_{ false }, is_stopping_{ false };

			const int sample_period_ns_{ 1000000 };

			void *rt_task_handle_{ nullptr };
			void *ec_master_handle_{ nullptr };

#ifdef UNIX
			ec_master_t* ec_master_;
#endif
			friend class Slave;
			friend class Master;
		};
		auto Master::loadXml(const aris::core::XmlDocument &xml_doc)->void
		{
			auto root_xml_ele = xml_doc.RootElement()->FirstChildElement("controller");

			if (!root_xml_ele)throw std::runtime_error("can't find controller element in xml file");

			loadXml(*root_xml_ele);
		}
		auto Master::loadXml(const aris::core::XmlElement &xml_ele)->void
		{
			Root::loadXml(xml_ele);

			imp_->data_logger_ = findByName("data_logger") == children().end() ? &add<DataLogger>("data_logger") : static_cast<DataLogger*>(&(*findByName("data_logger")));
			imp_->slave_type_pool_ = findByName("slave_type_pool") == children().end() ? &add<aris::core::ObjectPool<SlaveType, Element> >("slave_type_pool") : static_cast<aris::core::ObjectPool<SlaveType, Element> *>(&(*findByName("slave_type_pool")));
			imp_->slave_pool_ = findByName("slave_pool") == children().end() ? &add<aris::core::ObjectPool<Slave, Element> >("slave_pool") : static_cast<aris::core::ObjectPool<Slave, Element> *>(&(*findByName("slave_pool")));
			imp_->tx_data_pool_.clear();
			imp_->rx_data_pool_.clear();
			for (auto &slave : slavePool())
			{
				imp_->tx_data_pool_.push_back_ptr(&slave.txData());
				imp_->rx_data_pool_.push_back_ptr(&slave.rxData());
			}
		}
		auto Master::start()->void
		{
			if (imp_->is_running_)throw std::runtime_error("master already running");
			imp_->is_running_ = true;

			// init begin //
#ifdef UNIX
			if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) { throw std::runtime_error("lock failed"); }

			if (!(imp_->ec_master_ = ecrt_request_master(0))) { throw std::runtime_error("master request failed!"); }

			// init each slave and update tx & rx data pool //
			txDataPool().clear();
			rxDataPool().clear();
			for (auto &slave : slavePool())
			{
				slave.imp_->init();
				slave.init();
				txDataPool().push_back_ptr(&slave.txData());
				rxDataPool().push_back_ptr(&slave.rxData());
			}


			ecrt_master_activate(imp_->ec_master_);

			for (auto &slave : slavePool())slave.imp_->domain_pd_ = ecrt_domain_data(slave.imp_->domain_);
#endif

			imp_->rt_task_handle_ = aris_rt_task_start(&Imp::rt_task_func, this);
		};
		auto Master::stop()->void
		{
			if (!imp_->is_running_)throw std::runtime_error("master is not running, so can't stop");

			imp_->is_stopping_ = true;

			aris_rt_task_stop(imp_->rt_task_handle_);

#ifdef UNIX
			ecrt_master_deactivate(imp_->ec_master_);
			ecrt_release_master(imp_->ec_master_);

			for (auto&sla : slavePool())delete reinterpret_cast<SlaveDomainInfo*>(sla.imp_->ec_slave_handle_);
#endif
			imp_->is_stopping_ = false;
			imp_->is_running_ = false;
		}
		auto Master::slaveTypePool()->aris::core::ObjectPool<SlaveType, Element>& { return *imp_->slave_type_pool_; }
		auto Master::slaveTypePool()const->const aris::core::ObjectPool<SlaveType, Element>&{ return *imp_->slave_type_pool_; }
		auto Master::slavePool()->aris::core::ObjectPool<Slave, Element>& { return *imp_->slave_pool_; }
		auto Master::slavePool()const->const aris::core::ObjectPool<Slave, Element>&{ return *imp_->slave_pool_; }
		auto Master::txDataPool()->aris::core::RefPool<Slave::TxType> & { return imp_->tx_data_pool_; }
		auto Master::txDataPool()const->const aris::core::RefPool<Slave::TxType> &{return imp_->tx_data_pool_; }
		auto Master::rxDataPool()->aris::core::RefPool<Slave::RxType> & { return imp_->rx_data_pool_; }
		auto Master::rxDataPool()const->const aris::core::RefPool<Slave::RxType> &{return imp_->rx_data_pool_; }
		auto Master::dataLogger()->DataLogger& { return std::ref(*imp_->data_logger_); }
		auto Master::dataLogger()const->const DataLogger&{ return std::ref(*imp_->data_logger_); }
		Master::~Master() = default;
		Master::Master() :imp_(new Imp)
		{
			registerChildType<DataLogger>();

			registerChildType<Pdo>();
			registerChildType<Sdo>();
			registerChildType<PdoGroup>();
			registerChildType<aris::core::ObjectPool<Sdo, Element> >();
			registerChildType<aris::core::ObjectPool<PdoGroup, Element> >();
			registerChildType<aris::core::ObjectPool<SlaveType, Element> >();

			registerChildType<SlaveType>();
			registerChildType<Slave>();
			registerChildType<aris::core::ObjectPool<Slave, Element> >();
		}

		struct DataLogger::Imp
		{
			aris::core::Pipe *log_pipe_;
			aris::core::MsgFix<8196> log_data_msg_;
			std::size_t log_data_size_{ 0 };
			std::atomic_bool is_receiving_{ false }, is_sending_{ false }, is_prepaired_{ false };
            std::mutex mu_prepair_, mu_running_;
		};
		auto DataLogger::prepair(const std::string &log_file_name)->void
		{
			std::unique_lock<std::mutex> prepair_lck(imp_->mu_prepair_);
            std::unique_lock<std::mutex> running_lck(imp_->mu_running_, std::try_to_lock);
            if(!running_lck.owns_lock())throw std::runtime_error("failed to prepair pipe, because it's started already");
            running_lck.unlock();
            running_lck.release();

			auto file_name = aris::core::logDirPath() + (log_file_name.empty() ? "logdata_" + aris::core::logFileTimeFormat(std::chrono::system_clock::now()) + ".txt" : log_file_name);

			std::promise<void> thread_ready;
			auto fut = thread_ready.get_future();

			std::thread([this, file_name](std::promise<void> thread_ready)
			{
                std::unique_lock<std::mutex> running_lck(imp_->mu_running_);

				std::fstream file;
				file.open(file_name.c_str(), std::ios::out | std::ios::trunc);

				imp_->log_data_size_ = 0;
				for (auto &sla : master().slavePool()) imp_->log_data_size_ += sla.txTypeSize() + sla.rxTypeSize();
				imp_->log_data_msg_.resize(imp_->log_data_size_);

				//�����ɾ�pipe
				aris::core::MsgFix<8196> recv_msg;
				while (imp_->log_pipe_->recvMsg(recv_msg));
				imp_->is_receiving_ = true;
				thread_ready.set_value();
				
				long long count = 0;
				while (imp_->is_receiving_)
				{
					if (imp_->log_pipe_->recvMsg(recv_msg))
					{
						file << count++ << " ";

						std::size_t size_count = 0;
						for (auto &sla : master().slavePool())
						{
							sla.logData(*reinterpret_cast<Slave::TxType *>(recv_msg.data() + size_count)
								, *reinterpret_cast<Slave::RxType *>(recv_msg.data() + size_count + sla.txTypeSize()), file);

							file << " ";

							size_count += sla.txTypeSize() + sla.rxTypeSize();
						}
						file << std::endl;
					}
					else
					{
						std::this_thread::sleep_for(std::chrono::microseconds(10));
					}
				}
				file.close();
			}, std::move(thread_ready)).detach();

            fut.wait();

			imp_->is_prepaired_ = true;
		}
		auto DataLogger::start()->void
		{
			if (imp_->is_prepaired_)
			{
				imp_->is_sending_ = true;
				imp_->is_prepaired_ = false;
			}
		}
		auto DataLogger::stop()->void
		{
			imp_->is_sending_ = false;
			imp_->is_receiving_ = false;
		}
		auto DataLogger::logDataRT()->void
		{
			if (imp_->is_sending_)
			{
				std::size_t size_count = 0;
				for (auto &sla : master().slavePool())
				{
					auto tx_data_char = reinterpret_cast<char *>(&sla.txData());
					auto rx_data_char = reinterpret_cast<char *>(&sla.rxData());

					std::copy(tx_data_char, tx_data_char + sla.txTypeSize(), imp_->log_data_msg_.data() + size_count);
					size_count += sla.txTypeSize();
					std::copy(rx_data_char, rx_data_char + sla.rxTypeSize(), imp_->log_data_msg_.data() + size_count);
					size_count += sla.rxTypeSize();
				}

				imp_->log_pipe_->sendMsg(imp_->log_data_msg_);
			}
		}
		DataLogger::~DataLogger() = default;
		DataLogger::DataLogger(const std::string &name) :Element(name), imp_(new Imp) 
		{
			imp_->log_pipe_ = &add<aris::core::Pipe>("pipe", 16384);
		}
		DataLogger::DataLogger(Object &father, const aris::core::XmlElement &xml_ele) : Element(father, xml_ele), imp_(new Imp) 
		{
			imp_->log_pipe_ = findOrInsert<aris::core::Pipe>("pipe", 16384);
		}

		auto Element::master()->Master & { return static_cast<Master &>(root()); }
		auto Element::master()const->const Master &{ return static_cast<const Master &>(root()); }

		auto DO::slave()->Slave& { return *slave_; }
		auto DO::slave()const->const Slave&{ return *slave_; }

		auto Pdo::read(std::int32_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT32) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::int32_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::read(std::int16_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT16) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::int16_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::read(std::int8_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT8) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::int8_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::read(std::uint32_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT32) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::uint32_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::read(std::uint16_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT16) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::uint16_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::read(std::uint8_t &value)const->void
		{
			if (!static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT8) throw std::runtime_error("can not read pdo with wrong data type");
			value = *reinterpret_cast<const std::uint8_t*>(slave().imp_->domain_pd_ + offset_);
		}
		auto Pdo::write(std::int32_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT32) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::int32_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}
		auto Pdo::write(std::int16_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT16) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::int16_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}
		auto Pdo::write(std::int8_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != INT8) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::int8_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}
		auto Pdo::write(std::uint32_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT32) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::uint32_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}
		auto Pdo::write(std::uint16_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT16) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::uint16_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}
		auto Pdo::write(std::uint8_t value)->void
		{
			if (static_cast<const PdoGroup &>(father()).tx()) throw std::runtime_error("can not read pdo with rx type");
			if (dataType() != UINT8) throw std::runtime_error("can not read pdo with wrong data type");
			*reinterpret_cast<std::uint8_t*>(slave().imp_->domain_pd_ + offset_) = value;
		}

		struct Sdo::Imp
		{
			unsigned option_;
			union
			{
				char config_value_[8];
				std::uint32_t config_value_uint32_;
				std::uint16_t config_value_uint16_;
				std::uint8_t config_value_uint8_;
				std::int32_t config_value_int32_;
				std::int16_t config_value_int16_;
				std::int8_t config_value_int8_;
			};
		};
		auto Sdo::readable()const->bool { return (imp_->option_ & READ) != 0; }
		auto Sdo::writeable()const->bool { return (imp_->option_ & WRITE) != 0; }
		auto Sdo::configurable()const->bool { return (imp_->option_ & CONFIG) != 0; }
		auto Sdo::option()const->unsigned { return imp_->option_; }
		auto Sdo::configValueInt32()const->std::int32_t { return imp_->config_value_int32_; }
		auto Sdo::configValueInt16()const->std::int16_t { return imp_->config_value_int16_; }
		auto Sdo::configValueInt8()const->std::int8_t { return imp_->config_value_int8_; }
		auto Sdo::configValueUint32()const->std::uint32_t { return imp_->config_value_uint32_; }
		auto Sdo::configValueUint16()const->std::uint16_t { return imp_->config_value_uint16_; }
		auto Sdo::configValueUint8()const->std::uint8_t { return imp_->config_value_uint8_; }
		auto Sdo::getConfigValue(std::int32_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int32 type");
			value = imp_->config_value_int32_;
		}
		auto Sdo::getConfigValue(std::int16_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int16 type");
			value = imp_->config_value_int16_;
		}
		auto Sdo::getConfigValue(std::int8_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int8 type");
			value = imp_->config_value_int8_;
		}
		auto Sdo::getConfigValue(std::uint32_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint32 type");
			value = imp_->config_value_uint32_;
		}
		auto Sdo::getConfigValue(std::uint16_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint16 type");
			value = imp_->config_value_uint32_;
		}
		auto Sdo::getConfigValue(std::uint8_t &value)const->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint8 type");
			value = imp_->config_value_uint32_;
		}
		auto Sdo::setConfigValue(std::int32_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int32 type");
			imp_->config_value_int32_ = value;
		}
		auto Sdo::setConfigValue(std::int16_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int16 type");
			imp_->config_value_int16_ = value;
		}
		auto Sdo::setConfigValue(std::int8_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != INT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int8 type");
			imp_->config_value_int8_ = value;
		}
		auto Sdo::setConfigValue(std::uint32_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint32 type");
			imp_->config_value_uint32_ = value;
		}
		auto Sdo::setConfigValue(std::uint16_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint16 type");
			imp_->config_value_uint16_ = value;
		}
		auto Sdo::setConfigValue(std::uint8_t value)->void
		{
			if (!configurable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not configurable");
			if (dataType() != UINT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint8 type");
			imp_->config_value_uint8_ = value;
		}
		auto Sdo::read(std::int32_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != INT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int32 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::read(std::int16_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != INT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int16 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::read(std::int8_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != INT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int8 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::read(std::uint32_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != UINT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint32 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::read(std::uint16_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != UINT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint16 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::read(std::uint8_t &value)const->void
		{
			if (!readable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not readable");
			if (dataType() != UINT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint8 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_upload(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &real_size, &abort_code);
#endif
		}
		auto Sdo::write(std::int32_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != INT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int32 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		auto Sdo::write(std::int16_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != INT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int16 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		auto Sdo::write(std::int8_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != INT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not int8 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		auto Sdo::write(std::uint32_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != UINT32)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint32 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		auto Sdo::write(std::uint16_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != UINT16)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint16 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		auto Sdo::write(std::uint8_t value)->void
		{
			if (!writeable())throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not writeable");
			if (dataType() != UINT8)throw std::runtime_error("sdo " + std::to_string(index()) + "-" + std::to_string(subindex()) + " is not uint8 type");

#ifdef UNIX
			auto *ec_mst = master().imp_->ec_master_;
			std::size_t real_size;
			std::uint32_t abort_code;
			ecrt_master_sdo_download(ec_mst, slave().position(), index(), subindex(), reinterpret_cast<std::uint8_t *>(&value), dataSize(), &abort_code);
#endif
		}
		Sdo::Sdo(Object &father, const aris::core::XmlElement &xml_ele) :DO(father, xml_ele)
		{
			if (attributeBool(xml_ele, "read", true))imp_->option_ |= READ; else imp_->option_ &= ~READ;
			if (attributeBool(xml_ele, "write", true))imp_->option_ |= WRITE; else imp_->option_ &= ~WRITE;
			if (xml_ele.Attribute("config"))
			{
				if (!writeable())throw std::runtime_error("you can't config data in unwriteable sdo, error in \"" + std::string(xml_ele.name()) + "\" sdo");
				imp_->option_ |= CONFIG;
				switch (data_type_)
				{
				case aris::control::DO::INT32:
					imp_->config_value_int32_ = attributeInt32(xml_ele, "config");
					break;
				case aris::control::DO::INT16:
					imp_->config_value_int16_ = attributeInt16(xml_ele, "config");
					break;
				case aris::control::DO::INT8:
					imp_->config_value_int8_ = attributeInt8(xml_ele, "config");
					break;
				case aris::control::DO::UINT32:
					imp_->config_value_uint32_ = attributeUint32(xml_ele, "config");
					break;
				case aris::control::DO::UINT16:
					imp_->config_value_uint16_ = attributeUint16(xml_ele, "config");
					break;
				case aris::control::DO::UINT8:
					imp_->config_value_uint8_ = attributeUint8(xml_ele, "config");
					break;
				default:
					throw std::runtime_error("failed to get sdo config value");
					break;
				}
			}
		}

		struct PdoGroup::Imp
		{
			bool is_tx_;
			std::uint16_t index_;
		};
		auto PdoGroup::tx()const->bool { return imp_->is_tx_; }
		auto PdoGroup::rx()const->bool { return !imp_->is_tx_; }
		auto PdoGroup::index()const->std::uint16_t { return imp_->index_; }
		PdoGroup::PdoGroup(Object &father, const aris::core::XmlElement &xml_ele) :ObjectPool(father, xml_ele)
		{
			imp_->index_ = attributeUint16(xml_ele, "index");
			imp_->is_tx_ = attributeBool(xml_ele, "is_tx");
		}

		struct SlaveType::Imp
		{
			std::uint32_t product_code_, vender_id_;
			std::uint16_t alias_;
			std::uint32_t distributed_clock_;
		};
		auto SlaveType::productCode()const->std::uint32_t { return imp_->product_code_; }
		auto SlaveType::venderID()const->std::uint32_t { return imp_->vender_id_; }
		auto SlaveType::alias()const->std::uint16_t { return imp_->alias_; }
		auto SlaveType::distributedClock()const->std::uint32_t { return imp_->distributed_clock_; }
		SlaveType::SlaveType(Object &father, const aris::core::XmlElement &xml_ele) :Element(father, xml_ele)
		{
			imp_->product_code_ = attributeUint32(xml_ele, "product_code");
			imp_->vender_id_ = attributeUint32(xml_ele, "vender_id");
			imp_->alias_ = attributeUint16(xml_ele, "alias");
			imp_->distributed_clock_ = attributeUint32(xml_ele, "distributed_clock", 0);
		}

		auto Slave::Imp::init()->void
		{
#ifdef UNIX
			this->ec_slave_handle_ = aris_ecrt_slave_config(slave_->master().imp_->ec_master_, slave_);
#endif
		};
		auto Slave::productCode()const->std::uint32_t { return imp_->slave_type_->productCode(); }
		auto Slave::venderID()const->std::uint32_t { return imp_->slave_type_->venderID(); }
		auto Slave::alias()const->std::uint16_t { return imp_->slave_type_->alias(); }
		auto Slave::distributedClock()const->std::uint32_t { return imp_->slave_type_->distributedClock(); }
		auto Slave::txData()->TxType& { return imp_->tx_data_; }
		auto Slave::txData()const->const TxType&{ return imp_->tx_data_; }
		auto Slave::rxData()->RxType& { return imp_->rx_data_; }
		auto Slave::rxData()const->const RxType&{ return imp_->rx_data_; }
		auto Slave::pdoGroupPool()->aris::core::ObjectPool<PdoGroup, Element>& { return *imp_->pdo_group_pool_; }
		auto Slave::pdoGroupPool()const->const aris::core::ObjectPool<PdoGroup, Element>&{return *imp_->pdo_group_pool_; }
		auto Slave::sdoPool()->aris::core::ObjectPool<Sdo, Element>& { return *imp_->sdo_pool_; }
		auto Slave::sdoPool()const->const aris::core::ObjectPool<Sdo, Element>&{return *imp_->sdo_pool_; }
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::int8_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::int16_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::int32_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::uint8_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::uint16_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::readPdo(int pdo_group_id, int pdo_id, std::uint32_t &value) const->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).read(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::int8_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::int16_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::int32_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::uint8_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::uint16_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::writePdo(int pdo_group_id, int pdo_id, std::uint32_t value)->void
		{
			pdoGroupPool().at(pdo_group_id).at(pdo_id).write(value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readPdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t &value)const->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			readPdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::writePdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t value)->void
		{
			auto id_pair = imp_->pdo_map_.at(index).at(subindex);
			writePdo(id_pair.first, id_pair.second, value);
		}
		auto Slave::readSdoConfig(int sdoID, std::int8_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::readSdoConfig(int sdoID, std::int16_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::readSdoConfig(int sdoID, std::int32_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::readSdoConfig(int sdoID, std::uint8_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::readSdoConfig(int sdoID, std::uint16_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::readSdoConfig(int sdoID, std::uint32_t &value) const->void { sdoPool().at(sdoID).getConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::int8_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::int16_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::int32_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::uint8_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::uint16_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::configSdo(int sdoID, std::uint32_t value)->void { sdoPool().at(sdoID).setConfigValue(value); }
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::readSdoConfigIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdoConfig(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::configSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			configSdo(sdo_ID, value);
		}
		auto Slave::readSdo(int sdoID, std::int8_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::readSdo(int sdoID, std::int16_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::readSdo(int sdoID, std::int32_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::readSdo(int sdoID, std::uint8_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::readSdo(int sdoID, std::uint16_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::readSdo(int sdoID, std::uint32_t &value) const->void { sdoPool().at(sdoID).read(value); }
		auto Slave::writeSdo(int sdoID, std::int8_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::writeSdo(int sdoID, std::int16_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::writeSdo(int sdoID, std::int32_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::writeSdo(int sdoID, std::uint8_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::writeSdo(int sdoID, std::uint16_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::writeSdo(int sdoID, std::uint32_t value)->void { sdoPool().at(sdoID).write(value); }
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::readSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t &value)const->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			readSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int8_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int16_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::int32_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint8_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint16_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		auto Slave::writeSdoIndex(std::uint16_t index, std::uint8_t subindex, std::uint32_t value)->void
		{
			int sdo_ID = imp_->sdo_map_.at(index).at(subindex);
			writeSdo(sdo_ID, value);
		}
		Slave::~Slave() = default;
		Slave::Slave(Object &father, const aris::core::XmlElement &xml_ele) :Element(father, xml_ele), imp_(new Imp(this))
		{
			if (master().findByName("slave_type_pool") == master().children().end())
			{
				throw std::runtime_error("you must insert \"slave_type_pool\" before insert \"slave_pool\" node");
			}
			auto &slave_type_pool = static_cast<aris::core::ObjectPool<SlaveType, Element> &>(*master().findByName("slave_type_pool"));

			if (slave_type_pool.findByName(attributeString(xml_ele, "slave_type")) == slave_type_pool.end())
			{
				throw std::runtime_error("can not find slave_type \"" + attributeString(xml_ele, "slave_type") + "\" in slave \"" + name() + "\"");
			}
			imp_->slave_type_ = &add<SlaveType>(*slave_type_pool.findByName(attributeString(xml_ele, "slave_type")));
			imp_->pdo_group_pool_ = static_cast<aris::core::ObjectPool<PdoGroup, Element> *>(&*imp_->slave_type_->findByName("pdo_group_pool"));
			imp_->sdo_pool_ = static_cast<aris::core::ObjectPool<Sdo, Element> *>(&*imp_->slave_type_->findByName("sdo_pool"));

			for (auto &group : pdoGroupPool())for (auto &pdo : group)pdo.slave_ = this;
			for (auto &sdo : sdoPool())sdo.slave_ = this;

			// make PDO map //
			for (int i = 0; i < static_cast<int>(pdoGroupPool().size()); ++i)
			{
				auto &group = pdoGroupPool().at(i);
				for (int j = 0; j < static_cast<int>(group.size()); ++j)
				{
					auto &pdo = group.at(j);

					if (imp_->pdo_map_.find(pdo.index_) != imp_->pdo_map_.end())
					{
						imp_->pdo_map_.at(pdo.index_).insert(std::make_pair(pdo.subindex_, std::make_pair(i, j)));
					}
					else
					{
						std::map<std::uint8_t, std::pair<int, int> > subindex_map;
						subindex_map.insert(std::make_pair(pdo.subindex_, std::make_pair(i, j)));
						imp_->pdo_map_.insert(std::make_pair(pdo.index_, subindex_map));
					}
				}
			}

			// make SDO map //
			for (int i = 0; i < static_cast<int>(sdoPool().size()); ++i)
			{
				auto &sdo = sdoPool().at(i);
				if (imp_->sdo_map_.find(sdo.index_) != imp_->sdo_map_.end())
				{
					imp_->sdo_map_.at(sdo.index_).insert(std::make_pair(sdo.subindex_, i));
				}
				else
				{
					std::map<std::uint8_t, int > subindex_map;
					subindex_map.insert(std::make_pair(sdo.subindex_, i));
					imp_->sdo_map_.insert(std::make_pair(sdo.index_, subindex_map));
				}
			}
		}
	}
}
