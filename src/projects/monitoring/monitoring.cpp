//
// Created by getroot on 20. 1. 16.
//

#include "monitoring.h"
#include "monitoring_private.h"

namespace mon
{
	bool Monitoring::OnHostCreated(const info::Host &host_info)
	{
		std::unique_lock<std::mutex> lock(_map_guard);
		if(_hosts.find(host_info.GetId()) != _hosts.end())
		{
			return true;
		}
		auto host_metrics = std::make_shared<HostMetrics>(host_info);
		if (host_metrics == nullptr)
		{
			logte("Cannot create HostMetrics (%s)", host_info.GetName().CStr());
			return false;
		}
		
		_hosts[host_info.GetId()] = host_metrics;

		logti("Create HostMetrics(%s) for monitoring", host_info.GetName().CStr());
		return true;
	}
	bool Monitoring::OnHostDeleted(const info::Host &host_info)
	{
		std::unique_lock<std::mutex> lock(_map_guard);
		if (_hosts.find(host_info.GetId()) == _hosts.end())
		{
			return false;
		}
		_hosts.erase(host_info.GetId());

		logti("Delete HostMetrics(%s) for monitoring", host_info.GetName().CStr());
		return true;
	}
	bool Monitoring::OnApplicationCreated(const info::Application &app_info)
	{
		auto host_metrics = GetHostMetrics(app_info.GetHostInfo());
		if (host_metrics == nullptr)
		{
			return false;
		}

		return host_metrics->OnApplicationCreated(app_info);
	}
	bool Monitoring::OnApplicationDeleted(const info::Application &app_info)
	{
		auto host_metrics = GetHostMetrics(app_info.GetHostInfo());
		if (host_metrics == nullptr)
		{
			return false;
		}

		return host_metrics->OnApplicationDeleted(app_info);
	}
	bool Monitoring::OnStreamCreated(const info::Stream &stream)
	{
		auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metrics == nullptr)
		{
			return false;
		}

		return app_metrics->OnStreamCreated(stream);
	}
	bool Monitoring::OnStreamDeleted(const info::Stream &stream)
	{
		auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metrics == nullptr)
		{
			return false;
		}

		return app_metrics->OnStreamDeleted(stream);
	}

	std::shared_ptr<HostMetrics> Monitoring::GetHostMetrics(const info::Host &host_info)
	{
		if (_hosts.find(host_info.GetId()) == _hosts.end())
		{
			return nullptr;
		}

		return _hosts[host_info.GetId()];
	}

	std::shared_ptr<ApplicationMetrics> Monitoring::GetApplicationMetrics(const info::Application &app_info)
	{
		auto host_metric = GetHostMetrics(app_info.GetHostInfo());
		if (host_metric == nullptr)
		{
			return nullptr;
		}

		auto app_metric = host_metric->GetApplicationMetrics(app_info);
		if (app_metric == nullptr)
		{
			return nullptr;
		}

		return app_metric;
	}

	std::shared_ptr<StreamMetrics> Monitoring::GetStreamMetrics(const info::Stream &stream)
	{
		auto app_metric = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return nullptr;
		}

		auto stream_metric = app_metric->GetStreamMetrics(stream);
		if (stream_metric == nullptr)
		{
			// If the stream metrics is not exist, then create!
			if (!OnStreamCreated(stream))
			{
				return nullptr;
			}
		}

		return stream_metric;
	}
}  // namespace mon