//
//  ViewController.swift
//  stacker
//
//  Created by Евгений on 12.09.2026.
//

import NetworkExtension
import UIKit

class ViewController: UIViewController {

  override func viewDidLoad() {
    super.viewDidLoad()
    // Do any additional setup after loading the view.
    setupVPNManager(completion: manager_finish)
  }

  let manager_finish = { (manager: NETunnelProviderManager?) in
    guard let m = manager else {
      return
    }
    
    do {
      try m.connection.startVPNTunnel()
    } catch {
      
    }
  }
  
  
  func setupVPNManager(completion: @escaping (NETunnelProviderManager?) -> Void) {
      NETunnelProviderManager.loadAllFromPreferences { managers, error in
          guard error == nil else {
              completion(nil)
              return
          }
          
          let manager = managers?.first ?? NETunnelProviderManager()
          let proto = NETunnelProviderProtocol()
          
          proto.providerBundleIdentifier = "com.apoheliy.stacker.stacker-gate"
          proto.serverAddress = "192.0.2.1" // Replace with your VPN server address
          proto.providerConfiguration = ["server": "://example.com"]
          
          manager.protocolConfiguration = proto
          manager.localizedDescription = "My Custom VPN"
          manager.isEnabled = true
          
          manager.saveToPreferences { error in
              guard error == nil else {
                  completion(nil)
                  return
              }
              manager.loadFromPreferences { _ in
                  completion(manager)
              }
          }
      }
  }
}

