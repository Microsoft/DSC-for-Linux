require "rexml/document"
require "cgi"

require_relative 'patch_management_lib'
require_relative 'oms_common'
require_relative 'omslog'

module Fluent
  class LinuxUpdatesFilter < Filter

    Fluent::Plugin.register_filter('filter_patch_management', self)    

    def configure(conf)
      super
      @hostname = OMS::Common.get_hostname or "Unknown host"
      # do the usual configuration here
    end

    def start
      super
      # This is the first method to be called when it starts running
      # Use it to allocate resources, etc.
      # LinuxUpdates.log = @log
    end

    def shutdown
      super
      # This method is called when Fluentd is shutting down.
      # Use it to free up resources, etc.
    end

    def filter(tag, time, record)
      begin
        File.delete("/tmp/az-update-security.list")
        OMS::Log.info_once("File deleted successfully. /tmp/az-update-security.list")
      rescue Exception => e
        if e.errno!=2 #except no such file or directory issue. Errno: ENOENT = 2
          raise e
        end
      end

      xml_string = record['xml']
      OMS::Log.info_once("LinuxUpdates : Filtering xml size=#{xml_string.size}")
      linuxUpdates = LinuxUpdates.new()
      return linuxUpdates.transform_and_wrap(xml_string, @hostname, time)
    end # filter
  end # class
end # module