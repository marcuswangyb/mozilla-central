from sut import MockAgent
import mozdevice
import mozlog
import unittest
import hashlib
import tempfile
import os

class PushTest(unittest.TestCase):

    def test_push(self):
        pushfile = "1234ABCD"
        mdsum = hashlib.md5()
        mdsum.update(pushfile)
        expectedResponse = mdsum.hexdigest()

        # (good response, no exception), (bad response, exception)
        for response in [ (expectedResponse, False), ("BADHASH", True) ]:
            cmd = "push /mnt/sdcard/foobar %s" % len(pushfile)
            a = MockAgent(self, commands = [("isdir /mnt/sdcard", "TRUE"),
                                            (cmd, response[0])])
            exceptionThrown = False
            with tempfile.NamedTemporaryFile() as f:
                try:
                    f.write(pushfile)
                    f.flush()
                    d = mozdevice.DroidSUT("127.0.0.1", port=a.port)
                    d.pushFile(f.name, '/mnt/sdcard/foobar')
                except mozdevice.DMError, e:
                    exceptionThrown = True
                self.assertEqual(exceptionThrown, response[1])
            a.wait()

    def test_push_dir(self):
        pushfile = "1234ABCD"
        mdsum = hashlib.md5()
        mdsum.update(pushfile)
        expectedFileResponse = mdsum.hexdigest()

        tempdir = tempfile.mkdtemp()
        complex_path = os.path.join(tempdir, "baz")
        os.mkdir(complex_path)
        f = tempfile.NamedTemporaryFile(dir=complex_path)
        f.write(pushfile)
        f.flush()

        subTests = [ { 'cmds': [ ("isdir /mnt/sdcard//baz", "TRUE"),
                                 ("isdir /mnt/sdcard//baz", "TRUE"),
                                 ("push /mnt/sdcard//baz/%s %s" %
                                  (os.path.basename(f.name), len(pushfile)),
                                  expectedFileResponse) ],
                       'expectException': False },
                     { 'cmds': [ ("isdir /mnt/sdcard//baz", "TRUE"),
                                 ("isdir /mnt/sdcard//baz", "TRUE"),
                                 ("push /mnt/sdcard//baz/%s %s" %
                                  (os.path.basename(f.name), len(pushfile)),
                                  "BADHASH") ],
                       'expectException': True },
                     { 'cmds': [ ("isdir /mnt/sdcard//baz", "FALSE"),
                                 ("isdir /mnt", "FALSE"),
                                 ("mkdr /mnt",
                                  "##AGENT-WARNING## Could not create the directory /mnt") ],
                       'expectException': True },

                     ]

        for subTest in subTests:
            a = MockAgent(self, commands = subTest['cmds'])

            exceptionThrown = False
            try:
                d = mozdevice.DroidSUT("127.0.0.1", port=a.port,
                                       logLevel=mozlog.DEBUG)
                d.pushDir(tempdir, "/mnt/sdcard")
            except mozdevice.DMError, e:
                exceptionThrown = True
            self.assertEqual(exceptionThrown, subTest['expectException'])

            a.wait()

        # FIXME: delete directory when done

if __name__ == '__main__':
    unittest.main()
