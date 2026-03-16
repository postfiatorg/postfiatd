        if (db_.is_open())
        {
            nudb::error_code ec;
            db_.close(ec);
            if (ec)
            {
                if (ec == nudb::errc::no_such_file_or_directory)
                {
                    JLOG(j_.warn()) << "NuBD close() skipped missing file: "
                                    << ec.message();
                }
                else
                {
                    // Log to make sure the nature of the error gets to the user.
                    JLOG(j_.fatal()) << "NuBD close() failed: " << ec.message();
                    Throw<nudb::system_error>(ec);
                }
            }

            if (deletePath_)
            {
                boost::filesystem::remove_all(name_, ec);
                if (ec)
                {
                    JLOG(j_.fatal()) << "Filesystem remove_all of " << name_
                                     << " failed with: " << ec.message();
                }
            }
        }
